/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef PIDWRAPPERS_H
#define PIDWRAPPERS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "constants.h"
#include <sys/ptrace.h>
// This was needed for 64-bit SUSE LINUX Enterprise Server 9 (Linux 2.6.5):
#ifndef PTRACE_GETEVENTMSG
# include <linux/ptrace.h>
#endif
#include <stdarg.h>
#ifndef __arm__
# include <asm/ldt.h> // Needed for 'struct user_desc' (arg 6 of __clone)
#else
struct user_desc {int dummy;}; /* <asm/ldt.h> is missing in Ubuntu 11.10 */
#endif
#include <stdio.h>
#include <thread_db.h>
#include <sys/procfs.h>
#include <syslog.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>
#include <mqueue.h>

#if __GLIBC_PREREQ(2,5)
# define READLINK_RET_TYPE ssize_t
#else
# define READLINK_RET_TYPE int
#endif

#ifdef __cplusplus
extern "C"
{
#endif

  union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO (Linux-specific) */
  };

  void dmtcpResetPidPpid();
  void dmtcpResetTid(pid_t tid);

  LIB_PRIVATE void *_real_dlsym(void *handle, const char *symbol);

/* The following function are defined in pidwrappers.cpp */
  pid_t gettid();
  int tkill(int tid, int sig);
  int tgkill(int tgid, int tid, int sig);

#define FOREACH_PIDVIRT_WRAPPER(MACRO)\
  MACRO(fork)               \
  MACRO(__clone)            \
  MACRO(gettid)             \
  MACRO(tkill)              \
  MACRO(tgkill)             \
  MACRO(syscall)            \
  MACRO(shmget)             \
  MACRO(shmat)              \
  MACRO(shmdt)              \
  MACRO(shmctl)             \
  MACRO(semctl)             \
  MACRO(msgctl)             \
  MACRO(mq_notify)          \
  MACRO(getppid)            \
  MACRO(tcgetpgrp)          \
  MACRO(tcsetpgrp)          \
  MACRO(getpgrp)            \
  MACRO(setpgrp)            \
  MACRO(getpgid)            \
  MACRO(setpgid)            \
  MACRO(getsid)             \
  MACRO(setsid)             \
  MACRO(kill)               \
  MACRO(wait)               \
  MACRO(waitpid)            \
  MACRO(waitid)             \
  MACRO(wait3)              \
  MACRO(wait4)              \
  MACRO(ioctl)              \
  MACRO(setgid)             \
  MACRO(setuid)             \
  MACRO(ptrace )            \
  MACRO(pthread_exit )      \
  MACRO(fcntl)              \
  MACRO(open)               \
  MACRO(open64)             \
  MACRO(fopen)              \
  MACRO(fopen64)            \
  MACRO(__xstat)            \
  MACRO(__xstat64)          \
  MACRO(__lxstat)           \
  MACRO(__lxstat64)         \
  MACRO(readlink)

# define PIDVIRT_ENUM(x) pidvirt_enum_ ## x
# define PIDVIRT_GEN_ENUM(x) PIDVIRT_ENUM(x),
  typedef enum {
    FOREACH_PIDVIRT_WRAPPER(PIDVIRT_GEN_ENUM)
    numPidVirtWrappers
  } PidVirtWrapperOffset;

  pid_t _real_fork();
  int _real_clone ( int ( *fn ) ( void *arg ), void *child_stack, int flags,
                    void *arg, int *parent_tidptr, struct user_desc *newtls,
                    int *child_tidptr );

  pid_t _real_gettid(void);
  int   _real_tkill(int tid, int sig);
  int   _real_tgkill(int tgid, int tid, int sig);

  long int _real_syscall(long int sys_num, ... );

  /* System V shared memory */
  int _real_shmget(key_t key, size_t size, int shmflg);
  void* _real_shmat(int shmid, const void *shmaddr, int shmflg);
  int _real_shmdt(const void *shmaddr);
  int _real_shmctl(int shmid, int cmd, struct shmid_ds *buf);
  int _real_semctl(int semid, int semnum, int cmd, ...);
  int _real_msgctl(int msqid, int cmd, struct msqid_ds *buf);
  int _real_mq_notify(mqd_t mqdes, const struct sigevent *sevp);

  pid_t _real_getpid(void);
  pid_t _real_getppid(void);

  pid_t _real_tcgetpgrp(int fd);
  int   _real_tcsetpgrp(int fd, pid_t pgrp);

  pid_t _real_getpgrp(void);
  pid_t _real_setpgrp(void);

  pid_t _real_getpgid(pid_t pid);
  int   _real_setpgid(pid_t pid, pid_t pgid);

  pid_t _real_getsid(pid_t pid);
  pid_t _real_setsid(void);

  int   _real_kill(pid_t pid, int sig);

  pid_t _real_wait(__WAIT_STATUS stat_loc);
  pid_t _real_waitpid(pid_t pid, int *stat_loc, int options);
  int   _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);

  pid_t _real_wait3(__WAIT_STATUS status, int options, struct rusage *rusage);
  pid_t _real_wait4(pid_t pid, __WAIT_STATUS status, int options,
                    struct rusage *rusage);
  LIB_PRIVATE extern int send_sigwinch;
  int _real_ioctl(int d,  unsigned long int request, ...) __THROW;

  int _real_setgid(gid_t gid);
  int _real_setuid(uid_t uid);

  long _real_ptrace ( enum __ptrace_request request, pid_t pid, void *addr,
                    void *data);

  void _real_pthread_exit (void *retval);
  int _real_fcntl(int fd, int cmd, void *arg);

  int _real_open(const char *pathname, int flags, ...);
  int _real_open64(const char *pathname, int flags, ...);
  FILE* _real_fopen(const char *path, const char *mode);
  FILE* _real_fopen64(const char *path, const char *mode);
  int _real_xstat(int vers, const char *path, struct stat *buf);
  int _real_xstat64(int vers, const char *path, struct stat64 *buf);
  int _real_lxstat(int vers, const char *path, struct stat *buf);
  int _real_lxstat64(int vers, const char *path, struct stat64 *buf);
  READLINK_RET_TYPE _real_readlink(const char *path, char *buf, size_t bufsiz);

#ifdef __cplusplus
}
#endif

#endif

