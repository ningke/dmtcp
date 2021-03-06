/*****************************************************************************
 *   Copyright (C) 2006-2009 by Michael Rieker, Jason Ansel, Kapil Arya, and *
 *                                                            Gene Cooperman *
 *   mrieker@nii.net, jansel@csail.mit.edu, kapil@ccs.neu.edu, and           *
 *                                                          gene@ccs.neu.edu *
 *                                                                           *
 *   This file is part of the MTCP module of DMTCP (DMTCP:mtcp).             *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,       *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

/*
 * The goal of including this file is to define most of the external
 *   symbols used in mtcp_sharetemp.c .  This to insure that the linker
 *   does not try to resolve any symbols by linking in libc.  That would
 *   fail at restart time, when there is no libc.
 * mtcp_sharetemp.c is a concatenation
 *   of other files, allowing us to compile a single file into a static
 *   object.  (Is it still necessary to use sharetemp.c?)
 *
 *Could have used ideas from statifier?:
 *  http://statifier.sourceforge.net/
 *But that still depends too much on ELF
 *At least it has information on changing initialization, which we can use
 * later to remove the requirement to add a line to the `main' routine.
 *
 * Note that /usr/include/asm/unistd.h defines syscall using either:
 *   /usr/include/asm-i386/unistd.h
 * or:
 *   /usr/include/asm-x86_64/unistd.h
 */

#ifndef _MTCP_SYS_H
#define _MTCP_SYS_H

#include <stdio.h>
#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/version.h>

// Source code is taken from:  glibc-2.5/sysdeps/generic
/* Type to use for aligned memory operations.
   This should normally be the biggest type supported by a single load
   and store.  */
#undef opt_t
#undef OPSIZ
#define op_t    unsigned long int
#define OPSIZ   (sizeof(op_t))
#undef  __ptr_t
# define __ptr_t        void *

/* Type to use for unaligned operations.  */
typedef unsigned char byte;

/* Optimal type for storing bytes in registers.  */
#define reg_char        char


// From glibc-2.5/sysdeps/generic/memcopy.h:BYTE_COPY_FWD
// From glibc-2.5/sysdeps/generic/memcopy.h:BYTE_COPY_BWD
#define MTCP_BYTE_COPY_FWD(dst_bp, src_bp, nbytes)                            \
  do                                                                          \
    {                                                                         \
      size_t __nbytes = (nbytes);                                             \
      while (__nbytes > 0)                                                    \
        {                                                                     \
          byte __x = ((byte *) src_bp)[0];                                    \
          src_bp += 1;                                                        \
          __nbytes -= 1;                                                      \
          ((byte *) dst_bp)[0] = __x;                                         \
          dst_bp += 1;                                                        \
        }                                                                     \
    } while (0)
#define MTCP_BYTE_COPY_BWD(dst_ep, src_ep, nbytes)                            \
  do                                                                          \
    {                                                                         \
      size_t __nbytes = (nbytes);                                             \
      while (__nbytes > 0)                                                    \
        {                                                                     \
          byte __x;                                                           \
          src_ep -= 1;                                                        \
          __x = ((byte *) src_ep)[0];                                         \
          dst_ep -= 1;                                                        \
          __nbytes -= 1;                                                      \
          ((byte *) dst_ep)[0] = __x;                                         \
        }                                                                     \
    } while (0)

#ifdef MTCP_SYS_MEMMOVE
# ifndef _MTCP_MEMMOVE_
#  define _MTCP_MEMMOVE_
// From glibc-2.5/string/memmove.c
static void *
mtcp_sys_memmove (a1, a2, len)
     void *a1;
     const void *a2;
     size_t len;
{
  unsigned long int dstp = (long int) a1 /* dest */;
  unsigned long int srcp = (long int) a2 /* src */;

  /* This test makes the forward copying code be used whenever possible.
     Reduces the working set.  */
  if (dstp - srcp >= len)       /* *Unsigned* compare!  */
    {
      /* Copy from the beginning to the end.  */

      /* There are just a few bytes to copy.  Use byte memory operations.  */
      MTCP_BYTE_COPY_FWD (dstp, srcp, len);
    }
  else
    {
      /* Copy from the end to the beginning.  */
      srcp += len;
      dstp += len;

      /* There are just a few bytes to copy.  Use byte memory operations.  */
      MTCP_BYTE_COPY_BWD (dstp, srcp, len);
    }

  return (a1 /* dest */);
}
# endif
#endif

#ifdef MTCP_SYS_MEMCPY
# ifndef _MTCP_MEMCPY_
#  define _MTCP_MEMCPY_
// From glibc-2.5/string/memcpy.c; and
/* Copy exactly NBYTES bytes from SRC_BP to DST_BP,
   without any assumptions about alignment of the pointers.  */
static void *
mtcp_sys_memcpy (dstpp, srcpp, len)
     void *dstpp;
     const void *srcpp;
     size_t len;
{
  unsigned long int dstp = (long int) dstpp;
  unsigned long int srcp = (long int) srcpp;

  /* SHOULD DO INITIAL WORD COPY BEFORE THIS. */
  /* There are just a few bytes to copy.  Use byte memory operations.  */
  MTCP_BYTE_COPY_FWD(dstp, srcp, len);
  return dstpp;
}
# endif
#endif

#if 0 /*  DEMONSTRATE_BUG */

// From glibc-2.5/string/memcmp.c:memcmp at end.
#ifndef _MTCP_MEMCMP_
# define _MTCP_MEMCMP_
static int
mtcp_sys_memcmp (s1, s2, len)
     const __ptr_t s1;
     const __ptr_t s2;
     size_t len;
{
  op_t a0;
  op_t b0;
  long int srcp1 = (long int) s1;
  long int srcp2 = (long int) s2;
  op_t res;

  /* There are just a few bytes to compare.  Use byte memory operations.  */
  while (len != 0)
    {
      a0 = ((byte *) srcp1)[0];
      b0 = ((byte *) srcp2)[0];
      srcp1 += 1;
      srcp2 += 1;
      res = a0 - b0;
      if (res != 0)
        return res;
      len -= 1;
    }

  return 0;
}
#endif

#endif /* DEMONSTRATE_BUG */

#ifdef MTCP_SYS_STRCHR
# ifndef _MTCP_STRCHR_
#  define _MTCP_STRCHR_
//   The  strchr() function from earlier C library returns a ptr to the first
//   occurrence  of  c  (converted  to a  char) in string s, or a
//   null pointer  if  c  does  not  occur  in  the  string.
static char *mtcp_sys_strchr(const char *s, int c) {
  for (; *s != (char)'\0'; s++)
    if (*s == (char)c)
      return (char *)s;
  return NULL;
}
# endif
#endif

#ifdef MTCP_SYS_STRLEN
# ifndef _MTCP_STRLEN_
#  define _MTCP_STRLEN_
//   The  strlen() function from earlier C library calculates  the  length
//     of  the string s, not including the terminating `\0' character.
__attribute__ ((unused))  /* Not used in every file where included. */
static size_t mtcp_sys_strlen(const char *s) {
  size_t size = 0;
  for (; *s != (char)'\0'; s++)
    size++;
  return size;
}
# endif
#endif

#ifdef MTCP_SYS_STRCPY
# ifndef _MTCP_STRCPY_
#  define _MTCP_STRCPY_
static char * mtcp_sys_strcpy(char *dest, const char *source) {
  char *d = dest;
  for (; *source != (char)'\0'; source++)
    *d++ = *source;
  *d = '\0';
  return dest;
}
# endif
#endif

//======================================================================

// Rename it for cosmetic reasons.  We export mtcp_inline_syscall.
#define mtcp_inline_syscall(name, num_args, args...) \
                                        INLINE_SYSCALL(name, num_args, args)

/* We allocate this in mtcp_safemmap.c.  Files using mtcp_sys.h
 * are also linking with mtcp_safemmap.c.
 */
extern int mtcp_sys_errno;

// Define INLINE_SYSCALL.  In i386, need patch for 6 args

// sysdep-x86_64.h:
//   From glibc-2.5/sysdeps/unix/sysv/linux/x86_64/sysdep.h:
//     (define INLINE_SYSCALL)
// sysdep-i386.h:
//   Or glibc-2.5/sysdeps/unix/sysv/linux/i386/sysdep.h: (define INLINE_SYSCALL)
// But all further includes from sysdep-XXX.h have been commented out.

#ifdef __i386__
/* AFTER DMTCP RELEASE 1.2.5, MAKE USE_PROC_MAPS CASE THE ONLY CASE AND DO:
 * mv sysdep-i386-new.h sysdep-i386.h
 */
// THIS CASE fOR i386 NEEDS PATCHING FOR 6 ARGUMENT CASE, SUCH AS MMAP.
// IT ONLY TRIES TO HANDLE UP TO 5 ARGS.
# include "sysdep-i386.h"

# ifndef __PIC__
// NOTE:  Some misinformation on web and newer glibc:sysdep-i386.h says 6-arg
//   syscalls use:  eax, ebx, ecx, edx, esi, edi, ebp
//   Maybe this was true historically, but it really uses eax for syscall
//   number, and sets ebx to point to the 6 args (which typically are on stack).
#  define EXTRAVAR_6
#  define LOADARGS_6 \
    "sub $24,%%esp; mov %2,(%%esp); mov %3,4(%%esp); mov %4,8(%%esp);" \
    " mov %5,12(%%esp); mov %6,16(%%esp); mov %7,20(%%esp);" \
    " mov %%esp,%%ebx\n\t" /* sysdep-i386 then does:  mov %1,%%eax */
#  define RESTOREARGS_6 \
    RESTOREARGS_5 \
    "add $24,%%esp\n\t"
#  define ASMFMT_6(arg1, arg2, arg3, arg4, arg5, arg6) \
    ASMFMT_5(arg1, arg2, arg3, arg4, arg5), "0" (arg6)
# else
// TO SEE EXAMPLES OF MACROS, TRY:
//     cpp -fPIC -DPIC -dM mtcp_safemmap.c | grep '_5 '
// MODEL TEMPLATE IN sysdep-i386.h:
//  #define INTERNAL_SYSCALL(name, err, nr, args...)
//   ({
//     register unsigned int resultvar;
//     EXTRAVAR_##nr
//     asm volatile (
//     LOADARGS_##nr
//     "movl %1, %%eax\n\t"
//     "int $0x80\n\t"
//     RESTOREARGS_##nr
//     : "=a" (resultvar)
//     : "i" (__NR_##name) ASMFMT_##nr(args) : "memory", "cc");
//     (int) resultvar; })
// PIC uses ebp as base pointer for variables.  Save it last, retore it first.
#if 0
#  define EXTRAVAR_6 int _xv1, _xv2;
#  define LOADARGS_6 LOADARGS_5 "movl %%esp, %4\n\t" "movl %%ebp, %%esp\n\t" "movl %9, %%ebp\n\t"
#  define RESTOREARGS_6 "movl %%esp, %%ebp\n\t" "movl %4, %%esp\n\t" RESTOREARGS_5
#  define ASMFMT_6(arg1,arg2,arg3,arg4,arg5,arg6) , "0" (arg1), "m" (_xv1), "m" (_xv2), "c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5), "rm" (arg6)
#else
// NOTE:  Some misinformation on web and newer glibc:sysdep-i386.h says 6-arg
//   syscalls use:  eax, ebx, ecx, edx, esi, edi, ebp
//   Maybe this was true historically, but it really uses eax for syscall
//   number, and sets ebx to point to the 6 args (which typically are on stack).
// eax is free register, since it is set to syscall number just before: int 0x80
#  define EXTRAVAR_6
#  define LOADARGS_6 \
    "sub $28,%%esp; mov %2,(%%esp); mov %3,4(%%esp); mov %4,8(%%esp);" \
    " mov %5,12(%%esp); mov %6,16(%%esp); mov %7,%%eax; mov %%eax,20(%%esp);" \
    " mov %%ebx,24(%%esp); mov %%esp,%%ebx\n\t" \
   /* sysdep-i386 then does:  mov %1,%%eax */
#  define RESTOREARGS_6 "mov 24(%%esp),%%ebx\n\t" "add $28,%%esp\n\t"
/*
#  define ASMFMT_6(arg1, arg2, arg3, arg4, arg5, arg6) \
    ASMFMT_5(arg1, arg2, arg3, arg4, arg5), "rm" (arg6)
*/
#  define ASMFMT_6(arg1,arg2,arg3,arg4,arg5,arg6) , "0" (arg1), "c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5), "rm" (arg6)
#endif
# endif

#elif __x86_64__
# include "sysdep-x86_64.h"

#elif __arm__
// COPIED FROM:  glibc-ports-2.14/sysdeps/unix/sysv/linux/arm/eabi/sysdep.h
//   In turn, this calls "sysdep-arm.h" from .../linux/arm/sysdep.h
# include "sysdep-arm-eabi.h"

#else
# error "Missing sysdep.h file for this architecture."
#endif /* end __arm__ */

// FIXME:  Get rid of mtcp_sys_errno
//   Must first define multi-threaded errno when glibc not present.
#define __set_errno(Val) ( mtcp_sys_errno = (Val) ) /* required for sysdep-XXX.h */
// #define __set_errno(Val) ( errno = mtcp_sys_errno = (Val) ) /* required for sysdep-XXX.h */

// #include <sysdeps/unix/x86_64/sysdep.h>  is not needed.
// translate __NR_getpid to syscall # using i386 or x86_64
#include <asm/unistd.h>

/* getdents() fills up the buffer not with 'struct dirent's as might be
 * expected, but with custom 'struct linux_dirent's.  This structure, however,
 * must be manually defined.  This definition is taken from the getdents(2) man
 * page.
 */
struct linux_dirent {
    long 	  d_ino;
    off_t	  d_off;
    unsigned short d_reclen;
    char 	  d_name[];
};

//==================================================================

/* USAGE:  mtcp_inline_syscall:  second arg is number of args of system call */
#define mtcp_sys_read(args...)  mtcp_inline_syscall(read,3,args)
#define mtcp_sys_write(args...)  mtcp_inline_syscall(write,3,args)
#define mtcp_sys_lseek(args...)  mtcp_inline_syscall(lseek,3,args)
#define mtcp_sys_open(args...)  mtcp_inline_syscall(open,3,args)
    // mode  must  be  specified  when O_CREAT is in the flags, and is ignored
    //   otherwise.
#define mtcp_sys_open2(args...)  mtcp_sys_open(args,0777)
#define mtcp_sys_ftruncate(args...) mtcp_inline_syscall(ftruncate,2,args)
#define mtcp_sys_close(args...)  mtcp_inline_syscall(close,1,args)
#define mtcp_sys_access(args...)  mtcp_inline_syscall(access,2,args)
#define mtcp_sys_fchmod(args...)  mtcp_inline_syscall(fchmod,2,args)
#define mtcp_sys_rename(args...)  mtcp_inline_syscall(rename,2,args)
#define mtcp_sys_exit(args...)  mtcp_inline_syscall(exit,1,args)
#define mtcp_sys_pipe(args...)  mtcp_inline_syscall(pipe,1,args)
#define mtcp_sys_dup(args...)  mtcp_inline_syscall(dup,1,args)
#define mtcp_sys_dup2(args...)  mtcp_inline_syscall(dup2,2,args)
#define mtcp_sys_getpid(args...)  mtcp_inline_syscall(getpid,0)
#define mtcp_sys_getppid(args...)  mtcp_inline_syscall(getppid,0)
#define mtcp_sys_fork(args...)   mtcp_inline_syscall(fork,0)
#define mtcp_sys_vfork(args...)   mtcp_inline_syscall(vfork,0)
#define mtcp_sys_execve(args...)  mtcp_inline_syscall(execve,3,args)
#define mtcp_sys_wait4(args...)  mtcp_inline_syscall(wait4,4,args)
#define mtcp_sys_gettimeofday(args...)  mtcp_inline_syscall(gettimeofday,2,args)
#if defined(__i386__) || defined(__x86_64__)
# define mtcp_sys_mmap(args...)  (void *)mtcp_inline_syscall(mmap,6,args)
#elif defined(__arm__)
/* ARM Linux kernel doesn't support mmap: translate to newer mmap2 */
# define mtcp_sys_mmap(addr,length,prot,flags,fd,offset) \
   (void *)mtcp_inline_syscall(mmap2,6,addr,length,prot,flags,fd,offset/4096)
#else
# error "getrlimit kernel call not implemented in this architecture"
#endif
#define mtcp_sys_mremap(args...)  (void *)mtcp_inline_syscall(mremap,4,args)
#define mtcp_sys_munmap(args...)  mtcp_inline_syscall(munmap,2,args)
#define mtcp_sys_mprotect(args...)  mtcp_inline_syscall(mprotect,3,args)
#define mtcp_sys_nanosleep(args...)  mtcp_inline_syscall(nanosleep,2,args)
#define mtcp_sys_brk(args...)  (void *)(mtcp_inline_syscall(brk,1,args))
#define mtcp_sys_rt_sigaction(args...) mtcp_inline_syscall(rt_sigaction,4,args)
#define mtcp_sys_set_tid_address(args...) \
  mtcp_inline_syscall(set_tid_address,1,args)

//#define mtcp_sys_stat(args...) mtcp_inline_syscall(stat, 2, args)
#define mtcp_sys_getuid(args...) mtcp_inline_syscall(getuid, 0)
#define mtcp_sys_geteuid(args...) mtcp_inline_syscall(geteuid, 0)

#define mtcp_sys_personality(args...) mtcp_inline_syscall(personality, 1, args)
#define mtcp_sys_readlink(args...) mtcp_inline_syscall(readlink, 3, args)
#if defined(__i386__) || defined(__x86_64__)
  /* Should this be changed to use newer ugetrlimit kernel call? */
# define mtcp_sys_getrlimit(args...) mtcp_inline_syscall(getrlimit, 2, args)
#elif defined(__arm__)
  /* EABI ARM exclusively uses newer ugetrlimit kernel API, and not getrlimit */
# define mtcp_sys_getrlimit(args...) mtcp_inline_syscall(ugetrlimit, 2, args)
#else
# error "getrlimit kernel call not implemented in this architecture"
#endif
#define mtcp_sys_setrlimit(args...) mtcp_inline_syscall(setrlimit, 2, args)

#ifdef __NR_getdents
#define mtcp_sys_getdents(args...)  mtcp_inline_syscall(getdents,3,args)
   /* Note that getdents() does not fill the buf with 'struct dirent's, but
    * instead with 'struct linux_dirent's.  These must be defined manually, and
    * in our case have been defined earlier in this file. */
#endif
#ifdef __NR_getdents64
#define mtcp_sys_getdents64(args...)  mtcp_inline_syscall(getdents64,3,args)
#endif

#define mtcp_sys_fcntl2(args...) mtcp_inline_syscall(fcntl,2,args)
#define mtcp_sys_fcntl3(args...) mtcp_inline_syscall(fcntl,3,args)
#define mtcp_sys_mkdir(args...) mtcp_inline_syscall(mkdir,2,args)

/* These functions are not defined for x86_64. */
#ifdef __i386__
# define mtcp_sys_get_thread_area(args...) \
    mtcp_inline_syscall(get_thread_area,1,args)
# define mtcp_sys_set_thread_area(args...) \
    mtcp_inline_syscall(set_thread_area,1,args)
#endif

#ifdef __x86_64__
# include <asm/prctl.h>
# include <sys/prctl.h>
  /* struct user_desc * uinfo; */
  /* In Linux 2.6.9 for i386, uinfo->base_addr is
   *   correctly typed as unsigned long int.
   * In Linux 2.6.9, uinfo->base_addr is  incorrectly typed as
   *   unsigned int.  So, we'll just lie about the type.
   */
/* SuSE Linux Enterprise Server 9 uses Linux 2.6.5 and requires original
 * struct user_desc from /usr/include/.../ldt.h
 */
/* RHEL 4 (Update 3) / Rocks 4.1.1-2.0 has <linux/version.h> saying
 *  LINUX_VERSION_CODE is 2.4.20 (and UTS_RELEASE=2.4.20)
 *  while uname -r says 2.6.9-34.ELsmp.  Here, it acts like a version earlier
 *  than the above 2.6.9.  So, we conditionalize on its 2.4.20 version.
 */
# if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
   /* struct modify_ldt_ldt_s   was defined instead of   struct user_desc   */
#  define user_desc modify_ldt_ldt_s
# endif

# ifdef MTCP_SYS_GET_SET_THREAD_AREA
/* This allocation hack will work only if calls to mtcp_sys_get_thread_area
 * and mtcp_sys_get_thread_area are both inside the same file (mtcp.c).
 * This is all because get_thread_area is not implemented for x86_64.
 */
static unsigned long int myinfo_gs;

/* ARE THE _GS OPERATIONS NECESSARY? */
#  define mtcp_sys_get_thread_area(uinfo) \
    ( mtcp_inline_syscall(arch_prctl,2,ARCH_GET_FS, \
         (unsigned long int)(&(((struct user_desc *)uinfo)->base_addr))), \
      mtcp_inline_syscall(arch_prctl,2,ARCH_GET_GS, &myinfo_gs) \
    )
#  define mtcp_sys_set_thread_area(uinfo) \
    ( mtcp_inline_syscall(arch_prctl,2,ARCH_SET_FS, \
	*(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)), \
      mtcp_inline_syscall(arch_prctl,2,ARCH_SET_GS, myinfo_gs) \
    )
# endif /* end MTCP_SYS_GET_SET_THREAD_AREA */
#endif /* end __x86_64__ */

#ifdef __arm__
# ifdef MTCP_SYS_GET_SET_THREAD_AREA
/* This allocation hack will work only if calls to mtcp_sys_get_thread_area
 * and mtcp_sys_get_thread_area are both inside the same file (mtcp.c).
 * This is all because get_thread_area is not implemented for arm.
 *     For ARM, the thread pointer seems to point to the next slot
 * after the 'struct pthread'.  Why??  So, we subtract that address.
 * After that, tid/pid will be located at  offset 104/108 as expected
 * for glibc-2.13.
 * NOTE:  'struct pthread' defined in glibc/nptl/descr.h
 *     The value below (1216) is current for glibc-2.13.
 *     May have to update 'sizeof(struct pthread)' for new versions of glibc.
 *     We can automate this by searching for negative offset from end
 *     of 'struct pthread' in TLS_TID_OFFSET, TLS_PID_OFFSET in mtcp.c.
 */
static unsigned int myinfo_gs;

#  define mtcp_sys_get_thread_area(uinfo) \
  ({ asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t" \
                   : "=r" (myinfo_gs) ); \
    myinfo_gs = myinfo_gs - 1216; /* sizeof(struct pthread) = 1216 */ \
    *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr) \
      = myinfo_gs; \
    myinfo_gs; })
#  define mtcp_sys_set_thread_area(uinfo) \
    ( myinfo_gs = \
        *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr), \
      (mtcp_sys_kernel_set_tls(myinfo_gs+1216), 0) \
      /* 0 return value at end means success */ )
# endif /* end MTCP_SYS_GET_SET_THREAD_AREA */
#endif /* end __arm__ */

/*****************************************************************************
 * mtcp_sys_kernel_XXX() indicates it's particular to Linux, or glibc uses
 * a different version than the kernel version of the function.
 *
 * NOTE:  this calls kernel version of stat, not the glibc wrapper for stat
 * Needs: glibc_kernel_stat.h = glibc-2.5/sysdeps/unix/sysv/linux/kernel_stat.h
 *			for sake of st_mode, st_def, st_inode fields.
 *   For:	int stat(const char *file_name, struct kernel_stat *buf);
 *   For:	int lstat(const char *file_name, struct kernel_stat *buf);
 *
 * See glibc:/var/tmp/cooperma/glibc-2.5/sysdeps/unix/sysv/linux/i386/lxstat.c
 *   for other concerns about using stat in a 64-bit environment.
 *****************************************************************************/

/* NOTE:  MTCP no longer needs the following two mtcp_sys_kernel_stat so
 * commenting them out.                                            --Kapil
 *
 * #define mtcp_sys_kernel_stat(args...)  mtcp_inline_syscall(stat,2,args)
 * #define mtcp_sys_kernel_lstat(args...)  mtcp_inline_syscall(lstat,2,args)
 */

/* NOTE:  this calls kernel version of futex, not glibc sys_futex ("man futex")
 *   There is no library supporting futex.  Syscall is the only way to call it.
 *   For:	int futex(int *uaddr, int op, int val,
 *			 const struct timespec *timeout, int *uaddr2, int val3)
 *   "man 2 futex" and "man 4/7 futex" have limited descriptions.
 *   mtcp_internal.h has the macro defines used with futex.
 */
#define mtcp_sys_kernel_futex(args...)  mtcp_inline_syscall(futex,6,args)

/* NOTE:  this calls kernel version of gettid, not glibc gettid ("man gettid")
 *   There is no library supporting gettid.  Syscall is the only way to call it.
 *   For:	pid_t gettid(void);
 */
#define mtcp_sys_kernel_gettid(args...)  mtcp_inline_syscall(gettid,0)

/* NOTE:  this calls kernel version of tkill, not glibc tkill ("man tkill")
 *   There is no library supporting tkill.  Syscall is the only way to call it.
 *   For:	pid_t tkill(void);
 */
#define mtcp_sys_kernel_tkill(args...)  mtcp_inline_syscall(tkill,2,args)
#define mtcp_sys_kernel_tgkill(args...)  mtcp_inline_syscall(tgkill,3,args)

#if defined(__arm__)
/* NOTE:  set_tls is an ARM-specific call, with only a kernel API.
 *   We use the _RAW form;  otherwise, set_tls would expand to __ARM_set_tls
 *   This is a modification of sysdep-arm.h:INLINE_SYSCALL()
 */
# define INLINE_SYSCALL_RAW(name, nr, args...)                          \
  ({ unsigned int _sys_result = INTERNAL_SYSCALL_RAW (name, , nr, args);\
     if (__builtin_expect (INTERNAL_SYSCALL_ERROR_P (_sys_result, ), 0))\
       {                                                                \
         __set_errno (INTERNAL_SYSCALL_ERRNO (_sys_result, ));          \
         _sys_result = (unsigned int) -1;                               \
       }                                                                \
     (int) _sys_result; })
/* Next macro uses 'mcr', a kernel-mode instr. on ARM */
# define mtcp_sys_kernel_set_tls(args...)  \
  INLINE_SYSCALL_RAW(__ARM_NR_set_tls,1,args)
#endif

//==================================================================

#ifdef __x86_64__
# define eax rax
# define ebx rbx
# define ecx rcx
# define edx rax
# define ebp rbp
# define esi rsi
# define edi rdi
# define esp rsp
# define CLEAN_FOR_64_BIT(args...) CLEAN_FOR_64_BIT_HELPER(args)
# define CLEAN_FOR_64_BIT_HELPER(args...) #args
#elif __i386__
# define CLEAN_FOR_64_BIT(args...) #args
#else
# define CLEAN_FOR_64_BIT(args...) "CLEAN_FOR_64_BIT_undefined"
#endif

#if __arm__
/*
 * NOTE:  The following are not defined for __ARM_EABI__.  Each redirects
 * to a different kernel call.  See __NR_getrlimit__ for an example.
 * __NR_time, __NR_umount, __NR_stime, __NR_alarm, __NR_utime, __NR_getrlimit,
 * __NR_select, __NR_readdir, __NR_mmap, __NR_socketcall, __NR_syscall,
 * __NR_ipc, __NR_get_thread_area, __NR_set_thread_area
 */
#endif

#endif
