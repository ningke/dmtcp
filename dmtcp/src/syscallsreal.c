/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/


#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
// These next two are defined in features.h based on the user macros above.
// #define GNU_SRC
// #define __USE_UNIX98

#include <malloc.h>
#include <pthread.h>
#include "syscallwrappers.h"
#include "trampolines.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include "constants.h"
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
typedef int (*funcptr_t) ();
typedef pid_t (*funcptr_pid_t) ();
typedef funcptr_t (*signal_funcptr_t) ();

static pthread_mutex_t theMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

LIB_PRIVATE pid_t gettid() {
  return syscall(SYS_gettid);
}
LIB_PRIVATE int tkill(int tid, int sig) {
  return syscall(SYS_tkill, tid, sig);
}
LIB_PRIVATE int tgkill(int tgid, int tid, int sig) {
  return syscall(SYS_tgkill, tgid, tid, sig);
}

// FIXME: Are these primitives (_dmtcp_lock, _dmtcp_unlock) required anymore?
void _dmtcp_lock() { _real_pthread_mutex_lock (&theMutex); }
void _dmtcp_unlock() { _real_pthread_mutex_unlock (&theMutex); }

void _dmtcp_remutex_on_fork() {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
  pthread_mutex_init (&theMutex, &attr);
  pthread_mutexattr_destroy(&attr);
}
/*
 * DMTCP puts wrappers around several libc (also libpthread, libdl etc.)
 * functions in order to work. In these wrappers, DMTCP has to do some work
 * before and after the real function is called in libc.
 *
 * In order to call the real function in libc, DMTCP calculates the address of
 * the function in libc and calls that address directly. There are several
 * techniques of calculating the address of the libc function. In this
 * document, we briefly discuss the techniques that DMTCP has used in past and
 * how it evolved into the current design.
 *
 * History:
 * 1. dlopen/dlsym: Once upon a time :-), DMTCP used to dlopen "libc.so" and
 *    then call dlsym() on the libc handle to find the addresses of the libc
 *    functions wrapped by DMTCP.
 *
 * This worked great for a while until we needed wrappers for
 * malloc/calloc/free, etc. The reason was the fact that dlopen/dlsym/dlerror
 * internally call calloc to allocate a small buffer. As can be seen, dlopen
 * calls calloc which * goes to the DMTCP wrapper for calloc, which in turn
 * needs to call dlopen() to find the address of libc calloc and so this goes
 * into an infinite recursion.
 *
 * 2a. Libc-offsets - take 1: To counter the problems related to malloc/calloc
 *     wrappers, DMTCP was modified to not use dlopen/dlsym. Instead, a new
 *     mechanism was implemented.
 *
 *     While executing dmtcp_checkpoint, for each function wrapped by DMTCP, we
 *     calculated it's offset, in libc, from a known base-function (toupper, a
 *     function not wrapped by DMTCP) in libc, i.e. we do:
 *       open_offset = &open - &toupper;
 *     The offsets were passed along to dmtcphijack.so in an environment
 *     variable. To calculate the address of libc function now becomes very
 *     easy -- calculate the address of base-function, and add to it the offset
 *     of the required function i.e.
 *       open_libc_address = &toupper + open_offset;
 *
 *     The environment variable holding the offsets was made available to each
 *     and every new process created via fork/exec.
 *
 * This worked fine until we discovered that some applications put a wrapper
 * around toupper as well :(.
 *
 * 2b. Libc-offsets - take 2:2b. Libc-offsets - take 2: In the next iteration,
 *     we decided to use a heuristic based approach of using a pool of libc
 *     base-functions instead of just one. An average address of base-functions
 *     was calculated and that was used in offset calculations.
 *
 * This approach was fine until we decided to support process migration. If a
 * process is migrated to a different machine with a different version of libc,
 * the offsets that are stored in memory aren't correct anymore and so if the
 * migrated process creates a child process, the offsets won't work.
 *
 * 3. dlsym(RTLD_NEXT, symbol): This is the current approach. In the _real_XYZ
 *    function, we call dlsym(RTLD_NEXT, "XYZ") to calculate the address of
 *    function in the libraries that come after the current library in the
 *    search order (see man dlsym for more details).
 *
 * There are three problems with this scheme:
 * a) As with scheme 1 (dlopen/dlsym) -- if there are wrappers around
 *    calloc/free, it goes into an infinite recursion, and
 * b). Even if we don't have wrappers around calloc, there can be a problem if
 *     some application uses the malloc_hooks.
 *     (see http://www.gnu.org/s/hello/manual/libc/Hooks-for-Malloc.html).
 *     One notable example is libopen-pal.so (part of OpenMPI) which uses
 *     malloc_hooks and in the malloc hook, it called xstat() which landed in
 *     the DMTCP wrapper for xstat() and hence an infinite recursive loop.
 * c) Certain libpthread symbols are also defined in libc. For example, 'nm
 *    libc.so' reveals that 'pthread_cond_broadcast', 'pthread_cond_signal',
 *    and others are defined in libc.so. Thus, depending on the library load
 *    order, RTLD_NEXT might instead resolve to the libc version, which has
 *    been shown to cause problems (e.g. in the FReD record-replay plugin,
 *    which has wrappers around those functions).
 *
 * The work around to these problems is described in the following section.
 *
 * ***************************************************************************
 *
 * Current Workaround:
 *
 * In order to deal with the situation where we have malloc/calloc wrappers and
 * a potential application with malloc_hooks, we need to do the following:
 *
 * 0. Initialize all wrappers (calculate libc addr) before DMTCP does anything
 *    else i.e. do it at the beginning of the DmtcpWorker constructor.
 * 1. Define a variable dmtcp_wrappers_initializing, which is set to '1' while
 *    it is initializing and '0' after the * initialization has completed.
 * 2. Always have wrappers for malloc/calloc/free.
 * 3. In the wrappers for malloc/calloc/free, make sure that malloc hooks are
 *    never called. One way to do this is to disable malloc_hooks, but since
 *    they are not thread-safe, this is not a desired solution. Also note that
 *    malloc hooks have been deprecated in glibc 2.14 and will be removed in
 *    glibc 2.15.
 *
 *    Another way to avoid malloc hooks is to allocate memory using JALLOC to
 *    avoid calling libc:malloc. But we don't want to do this for all
 *    malloc/calloc calls, and so the call to JALLOC should be made only if
 *    dmtcp_wrappers_initializing is set to '1'.
 *
 *    There is a problem with the JALLOC approach too when using RECORD_REPLAY.
 *    RECORD_REPLAY puts wrappers around mmap() etc. and JALLOC uses mmap() to
 *    allocate memory :-(and as one can guess, it gets into a infinite
 *    recursion.
 * 4. The solution is to use static buffer when dlsym() calls calloc() during
 *    wrapper-initialization. It was noted that, calloc() is called only once
 *    with buf-size of 32, during dlsym() execution and thus it is safe to keep
 *    a small static buffer and pass on its address to the caller. The
 *    subsequent call to free() is ignored.
 *
 * In order to deal with the fact that libc.so contains some definition of
 * several pthread_* functions, we do the following. In initializing the
 * libpthread wrappers, we explicitly call dlopen() on libpthread.so. Then we
 * are guaranteed to resolve the symbol to the correct libpthread symbol.
 *
 * This solution is imperfect: if the user program also defines wrappers for
 * these functions, then using dlopen()/dlsym() explicitly on libpthread will
 * cause the user wrappers to be skipped. We have not yet run into a program
 * which does this, but it may occur in the future.
 */

LIB_PRIVATE void dmtcp_setThreadPerformingDlopenDlsym();
LIB_PRIVATE void dmtcp_unsetThreadPerformingDlopenDlsym();
extern void prepareDmtcpWrappers();
extern int dmtcp_wrappers_initializing;
static void *_real_func_addr[numLibcWrappers];
static int _libc_wrappers_initialized = 0;
static int _libpthread_wrappers_initialized = 0;

#ifndef DISABLE_PTHREAD_GETSPECIFIC_TRICK
static char wrapper_init_buf[1024];
static trampoline_info_t pthread_getspecific_trampoline_info;
void *_dmtcp_pthread_getspecific(pthread_key_t key)
{
  if (!dmtcp_wrappers_initializing) {
    fprintf(stderr, "DMTCP INTERNAL ERROR\n\n");
    abort();
  }
  pthread_setspecific(key, wrapper_init_buf);
  UNINSTALL_TRAMPOLINE(pthread_getspecific_trampoline_info);
  return pthread_getspecific(key);
}

static void _dmtcp_PreparePthreadGetSpecific()
{
  dmtcp_setup_trampoline_by_addr(&pthread_getspecific,
                                 (void*) &_dmtcp_pthread_getspecific,
                                 &pthread_getspecific_trampoline_info);
}
#endif

#define GET_FUNC_ADDR(name) \
  _real_func_addr[ENUM(name)] = _real_dlsym(RTLD_NEXT, #name);

LIB_PRIVATE
void initialize_libc_wrappers()
{
  const char *warn_msg =
    "WARNING: dmtcp_wrappers_initializing is set to '0' in the call to\n"
    "         initialize_wrappers(). This may not be a good sign. \n"
    "         Please inform dmtcp-developers if you see this message.\n\n";

  if (dmtcp_wrappers_initializing == 0) {
    _real_write(STDERR_FILENO, warn_msg, strlen(warn_msg));
    sleep(1);
    abort();
  }

  if (!_libc_wrappers_initialized) {
#ifndef DISABLE_PTHREAD_GETSPECIFIC_TRICK
    _dmtcp_PreparePthreadGetSpecific();
#endif
    FOREACH_DMTCP_WRAPPER(GET_FUNC_ADDR);
#ifdef __i386__
    /* On i386 systems, there are two pthread_create symbols. We want the one
     * with GLIBC_2.1 version. On 64-bit machines, there is only one
     * pthread_create symbol (GLIBC_2.2.5), so no worries there.
     */
    _real_func_addr[ENUM(pthread_create)] = dlvsym(RTLD_NEXT, "pthread_create",
                                                   "GLIBC_2.1");
#endif
    _libc_wrappers_initialized = 1;
  }
}

#define GET_LIBPTHREAD_FUNC_ADDR(name) \
  _real_func_addr[ENUM(name)] = dlvsym(RTLD_NEXT, #name, "GLIBC_2.3.2");

/*
 * WARNING: By using this method to initialize libpthread wrappers (direct
 * dlopen()/dlsym()) we are are overriding any user wrappers for these
 * functions. If this is a problem in the future we need to think of a new way
 * to do this.
 */
LIB_PRIVATE
void initialize_libpthread_wrappers()
{
  if (!_libpthread_wrappers_initialized) {
    FOREACH_LIBPTHREAD_WRAPPERS(GET_LIBPTHREAD_FUNC_ADDR);
    _libpthread_wrappers_initialized = 1;
  }
}

//////////////////////////
//// FIRST DEFINE REAL VERSIONS OF NEEDED FUNCTIONS

#define REAL_FUNC_PASSTHROUGH(name)  REAL_FUNC_PASSTHROUGH_TYPED(int, name)

#define REAL_FUNC_PASSTHROUGH_WORK(name) \
  if (fn == NULL) { \
    if (_real_func_addr[ENUM(name)] == NULL) prepareDmtcpWrappers(); \
    fn = _real_func_addr[ENUM(name)]; \
    if (fn == NULL) { \
      fprintf(stderr, "*** DMTCP: Error: lookup failed for %s.\n" \
                      "           The symbol wasn't found in current library" \
                      " loading sequence.\n" \
                      "    Aborting.\n", #name); \
      abort(); \
    } \
  }

#define REAL_FUNC_PASSTHROUGH_TYPED(type,name) \
  static type (*fn)() = NULL;                  \
  REAL_FUNC_PASSTHROUGH_WORK(name)             \
  return (*fn)

#define REAL_FUNC_PASSTHROUGH_VOID(name) \
  static void (*fn)() = NULL;            \
  REAL_FUNC_PASSTHROUGH_WORK(name)       \
  (*fn)

#define REAL_FUNC_PASSTHROUGH_NORETURN(name)                 \
  static void (*fn)() __attribute__ ((__noreturn__)) = NULL; \
  REAL_FUNC_PASSTHROUGH_WORK(name)                           \
  (*fn)

typedef void* (*dlsym_fnptr_t) (void *handle, const char *symbol);

LIB_PRIVATE
void *_dmtcp_get_libc_dlsym_addr()
{
  static dlsym_fnptr_t _libc_dlsym_fnptr = NULL;

  if (_libc_dlsym_fnptr == NULL) {
    long dlsym_offset = 0;
    if (getenv(ENV_VAR_DLSYM_OFFSET) == NULL) {
      fprintf(stderr,
              "%s:%d DMTCP Internal Error: Env var DMTCP_DLSYM_OFFSET not set.\n"
              "      Aborting.\n\n",
              __FILE__, __LINE__);
      abort();
    }

    dlsym_offset = (long) strtol(getenv(ENV_VAR_DLSYM_OFFSET), NULL, 10);
    _libc_dlsym_fnptr = (dlsym_fnptr_t)((char *)&LIBDL_BASE_FUNC + dlsym_offset);

    /* On Debian 5.0 (gcc-4.3.2 libc-2.7, ld-2.18.0), the call
     * by dmtcp_checkpoint to execvp fails without this call to unsetenv.
     * Possibly, execvp is calling dlsym even before dmtcphijack.so gets
     * loaded.
     */
    unsetenv (ENV_VAR_DLSYM_OFFSET);
  }

  return (void*) _libc_dlsym_fnptr;
}

LIB_PRIVATE
void *_real_dlsym (void *handle, const char *symbol) {
  static dlsym_fnptr_t _libc_dlsym_fnptr = NULL;
  if (_libc_dlsym_fnptr == NULL) {
    _libc_dlsym_fnptr = _dmtcp_get_libc_dlsym_addr();
  }

  // Avoid calling WRAPPER_EXECUTION_DISABLE_CKPT() in calloc() wrapper. See
  // comment in miscwrappers for more details.
  dmtcp_setThreadPerformingDlopenDlsym();
  void *res = (*_libc_dlsym_fnptr) (handle, symbol);
  dmtcp_unsetThreadPerformingDlopenDlsym();
  return res;
}

/* In dmtcphijack.so code always use this function instead of unsetenv.
 * Bash has its own implementation of getenv/setenv/unsetenv and keeps its own
 * environment equivalent to its shell variables. If DMTCP uses the bash
 * unsetenv, bash will unset its internal environment variable but won't remove
 * the process environment variable and yet on the next getenv, bash will
 * return the process environment variable.
 * This is arguably a bug in bash-3.2.
 */
LIB_PRIVATE
int _dmtcp_unsetenv(const char *name) {
  unsetenv (name);
  // One can fix this by doing a getenv() here and put a '\0' byte
  // at the start of the returned value, but that is not correct as if you do
  // another getenv after this, it would return "", which is not the same as
  // NULL.
  REAL_FUNC_PASSTHROUGH (unsetenv) (name);
}

LIB_PRIVATE
void *_real_dlopen(const char *filename, int flag) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, dlopen) (filename, flag);
}

LIB_PRIVATE
int _real_dlclose(void *handle) {
  REAL_FUNC_PASSTHROUGH_TYPED (int, dlclose) (handle);
}

LIB_PRIVATE
int _real_pthread_mutex_lock(pthread_mutex_t *mutex) {
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_mutex_lock) (mutex);
}

LIB_PRIVATE
int _real_pthread_mutex_trylock(pthread_mutex_t *mutex) {
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_mutex_trylock) (mutex);
}

LIB_PRIVATE
int _real_pthread_mutex_unlock(pthread_mutex_t *mutex) {
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_mutex_unlock) (mutex);
}

LIB_PRIVATE
int _real_pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_rwlock_unlock) (rwlock);
}

LIB_PRIVATE
int _real_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_rwlock_rdlock) (rwlock);
}

LIB_PRIVATE
int _real_pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock) {
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_rwlock_tryrdlock) (rwlock);
}

LIB_PRIVATE
int _real_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_rwlock_wrlock) (rwlock);
}

LIB_PRIVATE
int _real_pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock) {
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_rwlock_trywrlock) (rwlock);
}

LIB_PRIVATE
int _real_pthread_cond_broadcast(pthread_cond_t *cond)
{
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_cond_broadcast) (cond);
}

LIB_PRIVATE
int _real_pthread_cond_destroy(pthread_cond_t *cond)
{
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_cond_destroy) (cond);
}

LIB_PRIVATE
int _real_pthread_cond_init(pthread_cond_t *cond,
                            const pthread_condattr_t *attr)
{
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_cond_init) (cond,attr);
}

LIB_PRIVATE
int _real_pthread_cond_signal(pthread_cond_t *cond)
{
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_cond_signal) (cond);
}

LIB_PRIVATE
int _real_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                 const struct timespec *abstime)
{
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_cond_timedwait) (cond,mutex,abstime);
}

LIB_PRIVATE
int _real_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
  REAL_FUNC_PASSTHROUGH_TYPED (int,pthread_cond_wait) (cond,mutex);
}

LIB_PRIVATE
ssize_t _real_read(int fd, void *buf, size_t count) {
  REAL_FUNC_PASSTHROUGH (read) (fd,buf,count);
}

LIB_PRIVATE
ssize_t _real_write(int fd, const void *buf, size_t count) {
  REAL_FUNC_PASSTHROUGH_TYPED (ssize_t,write) (fd,buf,count);
}

LIB_PRIVATE
int _real_select(int nfds, fd_set *readfds, fd_set *writefds,
                 fd_set *exceptfds, struct timeval *timeout) {
  REAL_FUNC_PASSTHROUGH (select) (nfds,readfds,writefds,exceptfds,timeout);
}

LIB_PRIVATE
int _real_socket (int domain, int type, int protocol)
{
  REAL_FUNC_PASSTHROUGH (socket) (domain,type,protocol);
}

LIB_PRIVATE
int _real_connect (int sockfd, const struct sockaddr *serv_addr,
                    socklen_t addrlen)
{
  REAL_FUNC_PASSTHROUGH (connect) (sockfd,serv_addr,addrlen);
}

LIB_PRIVATE
int _real_bind (int sockfd, const struct sockaddr *my_addr,
                 socklen_t addrlen)
{
  REAL_FUNC_PASSTHROUGH (bind) (sockfd,my_addr,addrlen);
}

LIB_PRIVATE
int _real_listen (int sockfd, int backlog)
{
  REAL_FUNC_PASSTHROUGH (listen) (sockfd,backlog);
}

LIB_PRIVATE
int _real_accept (int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  REAL_FUNC_PASSTHROUGH (accept) (sockfd,addr,addrlen);
}

LIB_PRIVATE
int _real_accept4 (int sockfd, struct sockaddr *addr, socklen_t *addrlen,
                    int flags)
{
  REAL_FUNC_PASSTHROUGH (accept4) (sockfd,addr,addrlen,flags);
}

LIB_PRIVATE
int _real_setsockopt (int s, int level, int optname, const void *optval,
                       socklen_t optlen)
{
  REAL_FUNC_PASSTHROUGH (setsockopt) (s,level,optname,optval,optlen);
}

LIB_PRIVATE
int _real_getsockopt (int s, int level, int optname, void *optval,
                       socklen_t *optlen)
{
  REAL_FUNC_PASSTHROUGH (getsockopt) (s,level,optname,optval,optlen);
}

LIB_PRIVATE
int _real_fexecve (int fd, char *const argv[], char *const envp[])
{
  REAL_FUNC_PASSTHROUGH (fexecve) (fd,argv,envp);
}

LIB_PRIVATE
int _real_execve (const char *filename, char *const argv[],
                   char *const envp[])
{
  REAL_FUNC_PASSTHROUGH (execve) (filename,argv,envp);
}

LIB_PRIVATE
int _real_execv (const char *path, char *const argv[])
{
  REAL_FUNC_PASSTHROUGH (execv) (path,argv);
}

LIB_PRIVATE
int _real_execvp (const char *file, char *const argv[])
{
  REAL_FUNC_PASSTHROUGH (execvp) (file,argv);
}
LIB_PRIVATE
int _real_execvpe(const char *file, char *const argv[], char *const envp[]) {
  REAL_FUNC_PASSTHROUGH (execvpe) (file, argv, envp);
}

LIB_PRIVATE
int _real_system (const char *cmd)
{
  REAL_FUNC_PASSTHROUGH (system) (cmd);
}

LIB_PRIVATE
FILE *_real_popen(const char *command, const char *mode) {
  REAL_FUNC_PASSTHROUGH_TYPED (FILE*, popen) (command, mode);
}

LIB_PRIVATE
int _real_pclose(FILE *fp) {
  REAL_FUNC_PASSTHROUGH(pclose) (fp);
}

LIB_PRIVATE
pid_t _real_fork(void)
{
  REAL_FUNC_PASSTHROUGH_TYPED (pid_t, fork) ();
}

LIB_PRIVATE
int _real_close (int fd)
{
  REAL_FUNC_PASSTHROUGH (close) (fd);
}

LIB_PRIVATE
int _real_fclose (FILE *fp)
{
  REAL_FUNC_PASSTHROUGH (fclose) (fp);
}

LIB_PRIVATE
void _real_exit (int status)
{
  REAL_FUNC_PASSTHROUGH_VOID (exit) (status);
}

LIB_PRIVATE
int _real_getpt (void)
{
  REAL_FUNC_PASSTHROUGH (getpt) ();
}

LIB_PRIVATE
int _real_posix_openpt (int flags)
{
  REAL_FUNC_PASSTHROUGH (posix_openpt) (flags);
}

LIB_PRIVATE
int _real_ptsname_r (int fd, char * buf, size_t buflen)
{
  REAL_FUNC_PASSTHROUGH (ptsname_r) (fd, buf, buflen);
}

LIB_PRIVATE
int _real_socketpair (int d, int type, int protocol, int sv[2])
{
  REAL_FUNC_PASSTHROUGH (socketpair) (d,type,protocol,sv);
}

LIB_PRIVATE
void _real_openlog (const char *ident, int option, int facility)
{
  REAL_FUNC_PASSTHROUGH_VOID (openlog) (ident,option,facility);
}

LIB_PRIVATE
void _real_closelog (void)
{
  REAL_FUNC_PASSTHROUGH_VOID (closelog) ();
}

//set the handler
LIB_PRIVATE
sighandler_t _real_signal(int signum, sighandler_t handler) {
  REAL_FUNC_PASSTHROUGH_TYPED (sighandler_t, signal) (signum, handler);
}
LIB_PRIVATE
int _real_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
  REAL_FUNC_PASSTHROUGH (sigaction) (signum, act, oldact);
}
LIB_PRIVATE
int _real_sigvec(int signum, const struct sigvec *vec, struct sigvec *ovec) {
  REAL_FUNC_PASSTHROUGH (sigvec) (signum, vec, ovec);
}

//set the mask
LIB_PRIVATE
int _real_sigblock(int mask) {
  REAL_FUNC_PASSTHROUGH (sigblock) (mask);
}
LIB_PRIVATE
int _real_sigsetmask(int mask) {
  REAL_FUNC_PASSTHROUGH (sigsetmask) (mask);
}
LIB_PRIVATE
int _real_siggetmask(void) {
  REAL_FUNC_PASSTHROUGH (siggetmask)();
}
LIB_PRIVATE
int _real_sigprocmask(int how, const sigset_t *a, sigset_t *b) {
  REAL_FUNC_PASSTHROUGH (sigprocmask) (how, a, b);
}
LIB_PRIVATE
int _real_pthread_sigmask(int how, const sigset_t *a, sigset_t *b) {
  REAL_FUNC_PASSTHROUGH_TYPED (int, pthread_sigmask) (how, a, b);
}

LIB_PRIVATE
int _real_sigsuspend(const sigset_t *mask) {
  REAL_FUNC_PASSTHROUGH (sigsuspend) (mask);
}
LIB_PRIVATE
sighandler_t _real_sigset(int sig, sighandler_t disp)
{
  REAL_FUNC_PASSTHROUGH_TYPED (sighandler_t, sigset) (sig, disp);
}
LIB_PRIVATE
int _real_sighold(int sig) {
  REAL_FUNC_PASSTHROUGH (sighold) (sig);
}
LIB_PRIVATE
int _real_sigignore(int sig) {
  REAL_FUNC_PASSTHROUGH (sigignore) (sig);
}
// See 'man sigpause':  signal.h defines two possible versions for sigpause.
LIB_PRIVATE
int _real__sigpause(int __sig_or_mask, int __is_sig) {
  REAL_FUNC_PASSTHROUGH (__sigpause) (__sig_or_mask, __is_sig);
}
LIB_PRIVATE
int _real_sigpause(int sig) {
  REAL_FUNC_PASSTHROUGH (sigpause) (sig);
}
LIB_PRIVATE
int _real_sigrelse(int sig) {
  REAL_FUNC_PASSTHROUGH (sigrelse) (sig);
}

LIB_PRIVATE
int _real_sigwait(const sigset_t *set, int *sig) {
  REAL_FUNC_PASSTHROUGH (sigwait) (set, sig);
}
LIB_PRIVATE
int _real_sigwaitinfo(const sigset_t *set, siginfo_t *info) {
  REAL_FUNC_PASSTHROUGH (sigwaitinfo) (set, info);
}
LIB_PRIVATE
int _real_sigtimedwait(const sigset_t *set, siginfo_t *info,
                       const struct timespec *timeout) {
  REAL_FUNC_PASSTHROUGH (sigtimedwait) (set, info, timeout);
}


// gettid / tkill / tgkill are not defined in libc.
// So, this is needed even if there is no PID_VIRTUALIZATION
LIB_PRIVATE
pid_t _real_gettid(void) {
  // No glibc wrapper for gettid, although even if it had one, we would have
  // the issues similar to getpid/getppid().
  return (pid_t) _real_syscall(SYS_gettid);
}

LIB_PRIVATE
int   _real_tkill(int tid, int sig) {
  // No glibc wrapper for tkill, although even if it had one, we would have
  // the issues similar to getpid/getppid().
  return (int) _real_syscall(SYS_tkill, tid, sig);
}

LIB_PRIVATE
int   _real_tgkill(int tgid, int tid, int sig) {
  // No glibc wrapper for tgkill, although even if it had one, we would have
  // the issues similar to getpid/getppid().
  return (int) _real_syscall(SYS_tgkill, tgid, tid, sig);
}

LIB_PRIVATE
int _real_open(const char *pathname, int flags, ...) {
  mode_t mode = 0;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
  REAL_FUNC_PASSTHROUGH (open) (pathname, flags, mode);
}

LIB_PRIVATE
int _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
  REAL_FUNC_PASSTHROUGH (waitid) (idtype, id, infop, options);
}

LIB_PRIVATE
pid_t _real_wait4(pid_t pid, __WAIT_STATUS status, int options,
                  struct rusage *rusage) {
  REAL_FUNC_PASSTHROUGH_TYPED (pid_t, wait4) (pid, status, options, rusage);
}

LIB_PRIVATE
int _real_open64(const char *pathname, int flags, ...) {
  mode_t mode = 0;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
  REAL_FUNC_PASSTHROUGH (open) (pathname, flags, mode);
}

LIB_PRIVATE
FILE * _real_fopen(const char *path, const char *mode) {
  REAL_FUNC_PASSTHROUGH_TYPED (FILE *, fopen) (path, mode);
}

LIB_PRIVATE
FILE * _real_fopen64(const char *path, const char *mode) {
  REAL_FUNC_PASSTHROUGH_TYPED (FILE *, fopen64) (path, mode);
}

/* See comments for syscall wrapper */
LIB_PRIVATE
long int _real_syscall(long int sys_num, ...) {
  int i;
  void * arg[7];
  va_list ap;

  va_start(ap, sys_num);
  for (i = 0; i < 7; i++)
    arg[i] = va_arg(ap, void *);
  va_end(ap);

  // /usr/include/unistd.h says syscall returns long int (contrary to man page)
  REAL_FUNC_PASSTHROUGH_TYPED (long int, syscall) (sys_num, arg[0], arg[1],
                                                      arg[2], arg[3], arg[4],
                                                      arg[5], arg[6]);
}

LIB_PRIVATE
int _real_xstat(int vers, const char *path, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH (__xstat) (vers, path, buf);
}

LIB_PRIVATE
int _real_xstat64(int vers, const char *path, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH (__xstat64) (vers, path, buf);
}

LIB_PRIVATE
int _real_lxstat(int vers, const char *path, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH (__lxstat) (vers, path, buf);
}

LIB_PRIVATE
int _real_lxstat64(int vers, const char *path, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH (__lxstat64) (vers, path, buf);
}

LIB_PRIVATE
ssize_t _real_readlink(const char *path, char *buf, size_t bufsiz) {
  REAL_FUNC_PASSTHROUGH_TYPED (ssize_t, readlink) (path, buf, bufsiz);
}

LIB_PRIVATE
int _real_clone (int (*function) (void *), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr)
{
  REAL_FUNC_PASSTHROUGH (__clone) (function, child_stack, flags, arg,
                                      parent_tidptr, newtls, child_tidptr);
}

LIB_PRIVATE
int _real_pthread_tryjoin_np(pthread_t thread, void **retval) {
  REAL_FUNC_PASSTHROUGH_TYPED (int, pthread_tryjoin_np) (thread, retval);
}

LIB_PRIVATE
int _real_pthread_timedjoin_np(pthread_t thread, void **retval,
                                 const struct timespec *abstime) {
  REAL_FUNC_PASSTHROUGH_TYPED (int, pthread_timedjoin_np) (thread, retval, abstime);
}

LIB_PRIVATE
int _real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start_routine)(void*), void *arg) {
  REAL_FUNC_PASSTHROUGH_TYPED (int, pthread_create)
    (thread,attr,start_routine,arg);
}

//void _real_pthread_exit(void *retval) __attribute__ ((__noreturn__));
LIB_PRIVATE
void _real_pthread_exit(void *retval) {
  REAL_FUNC_PASSTHROUGH_NORETURN (pthread_exit) (retval);
}

LIB_PRIVATE
int _real_shmget (int key, size_t size, int shmflg) {
  REAL_FUNC_PASSTHROUGH (shmget) (key, size, shmflg);
}

LIB_PRIVATE
void* _real_shmat (int shmid, const void *shmaddr, int shmflg) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, shmat) (shmid, shmaddr, shmflg);
}

LIB_PRIVATE
int _real_shmdt (const void *shmaddr) {
  REAL_FUNC_PASSTHROUGH (shmdt) (shmaddr);
}

/* glibc provides two versions of shmctl: 2.0 and 2.2. For some reason, the
 * dlsym(RTLD_NEXT,...) is getting us the 2.0 version causing the wrong
 * function call. For i386 architecture, we need to pass IPC_64 to the system
 * call in order to work properly. Please refer to NOTES section of shmctl
 * manpage.
 */
#ifndef IPC_64
// Taken from <linux/ipc.h>
# define IPC_64  0x0100  /* New version (support 32-bit UIDs, bigger
                          message sizes, etc. */
#endif
#ifdef __i386__
# define IPC64_FLAG IPC_64
#else
# define IPC64_FLAG 0
#endif

LIB_PRIVATE
int _real_shmctl (int shmid, int cmd, struct shmid_ds *buf) {
  REAL_FUNC_PASSTHROUGH (shmctl) (shmid, cmd | IPC64_FLAG, buf);
}

LIB_PRIVATE
int _real_semget(key_t key, int nsems, int semflg) {
  REAL_FUNC_PASSTHROUGH (semget) (key, nsems, semflg);
}

LIB_PRIVATE
int _real_semop(int semid, struct sembuf *sops, size_t nsops) {
  REAL_FUNC_PASSTHROUGH (semop) (semid, sops, nsops);
}

LIB_PRIVATE
int _real_semtimedop(int semid, struct sembuf *sops, size_t nsops,
                     const struct timespec *timeout) {
  REAL_FUNC_PASSTHROUGH (semtimedop) (semid, sops, nsops, timeout);
}

LIB_PRIVATE
int _real_semctl(int semid, int semnum, int cmd, ...) {
  union semun uarg;
  va_list arg;
  va_start (arg, cmd);
  uarg = va_arg (arg, union semun);
  va_end (arg);
  REAL_FUNC_PASSTHROUGH (semctl) (semid, semnum, cmd | IPC64_FLAG, uarg);
}

LIB_PRIVATE
int _real_msgget(key_t key, int msgflg) {
  REAL_FUNC_PASSTHROUGH (msgget) (key, msgflg);
}

LIB_PRIVATE
int _real_msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg) {
  REAL_FUNC_PASSTHROUGH (msgsnd) (msqid, msgp, msgsz, msgflg);
}

LIB_PRIVATE
ssize_t _real_msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp,
                     int msgflg) {
  REAL_FUNC_PASSTHROUGH (msgrcv) (msqid, msgp, msgsz, msgtyp, msgflg);
}

LIB_PRIVATE
int _real_msgctl(int msqid, int cmd, struct msqid_ds *buf) {
  REAL_FUNC_PASSTHROUGH (msgctl) (msqid, cmd | IPC64_FLAG, buf);
}


LIB_PRIVATE
mqd_t _real_mq_open(const char *name, int oflag, mode_t mode,
                      struct mq_attr *attr) {
  REAL_FUNC_PASSTHROUGH_TYPED (mqd_t, mq_open) (name, oflag, mode, attr);
}

LIB_PRIVATE
int _real_mq_close(mqd_t mqdes) {
  REAL_FUNC_PASSTHROUGH (mq_close) (mqdes);
}

LIB_PRIVATE
int _real_mq_notify(mqd_t mqdes, const struct sigevent *sevp) {
  REAL_FUNC_PASSTHROUGH (mq_notify) (mqdes, sevp);
}

LIB_PRIVATE
ssize_t _real_mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                              unsigned int *msg_prio,
                              const struct timespec *abs_timeout) {
  REAL_FUNC_PASSTHROUGH_TYPED (ssize_t, mq_timedreceive) (mqdes, msg_ptr,
                                                          msg_len, msg_prio,
                                                          abs_timeout);
}

LIB_PRIVATE
int _real_mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                       unsigned int msg_prio,
                       const struct timespec *abs_timeout) {
  REAL_FUNC_PASSTHROUGH (mq_timedsend) (mqdes, msg_ptr, msg_len, msg_prio,
                                        abs_timeout);
}


LIB_PRIVATE
pid_t _real_getpid() {
  REAL_FUNC_PASSTHROUGH_TYPED(pid_t, getpid) ();
}

LIB_PRIVATE
void * _real_calloc(size_t nmemb, size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED(void*, calloc) (nmemb, size);
}

LIB_PRIVATE
void * _real_malloc(size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, malloc) (size);
}

LIB_PRIVATE
void * _real_realloc(void *ptr, size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, realloc) (ptr, size);
}

LIB_PRIVATE
void * _real_libc_memalign(size_t boundary, size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, __libc_memalign) (boundary, size);
}

LIB_PRIVATE
void _real_free(void *ptr) {
  REAL_FUNC_PASSTHROUGH_VOID (free) (ptr);
}

LIB_PRIVATE
void *_real_mmap(void *addr, size_t length, int prot, int flags,
    int fd, off_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, mmap) (addr,length,prot,flags,fd,offset);
}

LIB_PRIVATE
void *_real_mmap64(void *addr, size_t length, int prot, int flags,
    int fd, off64_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*,mmap64) (addr,length,prot,flags,fd,offset);
}

#if __GLIBC_PREREQ (2,4)
LIB_PRIVATE
void *_real_mremap(void *old_address, size_t old_size, size_t new_size,
    int flags, ... /* void *new_address*/) {
  if (flags == MREMAP_FIXED) {
    va_list ap;
    va_start(ap, flags);
    void *new_address = va_arg (ap, void *);
    va_end (ap);
    REAL_FUNC_PASSTHROUGH_TYPED (void*, mremap)
      (old_address, old_size, new_size, flags, new_address);
  } else {
    REAL_FUNC_PASSTHROUGH_TYPED (void*, mremap)
      (old_address, old_size, new_size, flags);
  }
}
#else
LIB_PRIVATE
void *_real_mremap(void *old_address, size_t old_size, size_t new_size,
    int flags) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, mremap)
    (old_address, old_size, new_size, flags);
}
#endif

LIB_PRIVATE
int _real_munmap(void *addr, size_t length) {
  REAL_FUNC_PASSTHROUGH_TYPED (int, munmap) (addr, length);
}

LIB_PRIVATE
int _real_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
  REAL_FUNC_PASSTHROUGH (poll) (fds, nfds, timeout);
}


LIB_PRIVATE
int _real_epoll_create(int size) {
  REAL_FUNC_PASSTHROUGH (epoll_create) (size);
}

LIB_PRIVATE
int _real_epoll_create1(int flags) {
  REAL_FUNC_PASSTHROUGH (epoll_create1) (flags);
}

LIB_PRIVATE
int _real_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
  REAL_FUNC_PASSTHROUGH (epoll_ctl) (epfd, op, fd, event);
}

LIB_PRIVATE
int _real_epoll_wait(int epfd, struct epoll_event *events,
                     int maxevents, int timeout) {
  REAL_FUNC_PASSTHROUGH (epoll_wait) (epfd, events, maxevents, timeout);
}

LIB_PRIVATE
int _real_epoll_pwait(int epfd, struct epoll_event *events,
                      int maxevents, int timeout, const sigset_t *sigmask) {
  REAL_FUNC_PASSTHROUGH (epoll_pwait) (epfd, events,
                                       maxevents, timeout, sigmask);
}

LIB_PRIVATE
int _real_eventfd (int initval, int flags) {
  REAL_FUNC_PASSTHROUGH (eventfd) (initval, flags);
}

LIB_PRIVATE
int _real_signalfd (int fd, const sigset_t *mask, int flags) {
  REAL_FUNC_PASSTHROUGH (signalfd) (fd, mask, flags);
}

/*===============================
   real inotify function calls
  ===============================*/
LIB_PRIVATE
int _real_inotify_init(void) {
  REAL_FUNC_PASSTHROUGH (inotify_init) ( );
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27) && __GLIBC_PREREQ(2,4)
LIB_PRIVATE
int _real_inotify_init1(int flags) {
  REAL_FUNC_PASSTHROUGH (inotify_init1) (flags);
}
#endif

LIB_PRIVATE
int _real_inotify_add_watch(int fd, const char *pathname, uint32_t mask) {
  REAL_FUNC_PASSTHROUGH (inotify_add_watch) (fd, pathname, mask);
}

LIB_PRIVATE
int _real_inotify_rm_watch(int fd, int wd) {
  REAL_FUNC_PASSTHROUGH (inotify_rm_watch) (fd, wd);
}
