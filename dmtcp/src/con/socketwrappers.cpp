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

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "sockettable.h"
#include <pthread.h>
#include <sys/select.h>
#include <sys/un.h>
#include <arpa/inet.h>

/* According to earlier standards */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "syscallwrappers.h"
#include "dmtcpworker.h"
#include "connection.h"
#include "connectionmanager.h"
#include "../jalib/jassert.h"
#include "../jalib/jfilesystem.h"

/*
 * XXX: TODO: Add wrapper protection for socket() family of system calls
 */

//////////////////////////////////////
//////// Now we define our wrappers

/* Prevent recursive calls to dmtcp_on_XXX() */
static int in_dmtcp_on_helper_fnc = 0;

#define PASSTHROUGH_DMTCP_HELPER(func, ...) {       \
    int ret = _real_ ## func(__VA_ARGS__);         \
    int saved_errno;                                \
    saved_errno = errno;                            \
    PASSTHROUGH_DMTCP_HELPER2(func,__VA_ARGS__);    \
    }

#define PASSTHROUGH_DMTCP_HELPER2(func, ...) {                              \
    _dmtcp_lock();                                                          \
    if (in_dmtcp_on_helper_fnc == 0) {                                      \
      in_dmtcp_on_helper_fnc = 1;                                           \
      if (ret < 0) ret = dmtcp_on_error(ret, sockfd, #func, saved_errno);    \
      else ret = dmtcp_on_ ## func(ret, __VA_ARGS__);                      \
      in_dmtcp_on_helper_fnc = 0;                                           \
    }                                                                       \
    _dmtcp_unlock();                                                        \
    errno =saved_errno;                                                     \
    /* If the wrapper-execution lock was acquired earlier, release it now*/ \
    WRAPPER_EXECUTION_ENABLE_CKPT()                                         \
    return ret;                                                             \
  }

extern "C" int socket(int domain, int type, int protocol)
{
  static int sockfd = -1;
  WRAPPER_EXECUTION_DISABLE_CKPT(); // The lock is released inside the macro.
  PASSTHROUGH_DMTCP_HELPER(socket, domain, type, protocol);
}

extern "C" int connect(int sockfd, const struct sockaddr *serv_addr,
                       socklen_t addrlen)
{
  WRAPPER_EXECUTION_DISABLE_CKPT(); // The lock is released inside the macro.
  int ret = _real_connect(sockfd,serv_addr,addrlen);
  int saved_errno = errno;

  //no blocking connect... need to hang around until it is writable
  if (ret < 0 && errno == EINPROGRESS)
  {
    fd_set wfds;
    struct timeval tv;
    int retval;

    FD_ZERO(&wfds);
    FD_SET(sockfd, &wfds);

    tv.tv_sec = 15;
    tv.tv_usec = 0;

    retval = select(sockfd+1, NULL, &wfds, NULL, &tv);
    /* Don't rely on the value of tv now! */

    if (retval == -1)
      perror("select()");
    else if (FD_ISSET(sockfd, &wfds))
    {
      int val = -1;
      socklen_t sz = sizeof(val);
      getsockopt(sockfd,SOL_SOCKET,SO_ERROR,&val,&sz);
      if (val==0) ret = 0;
    }
    else
      JTRACE("No data within five seconds.");
  }

  saved_errno = errno;
  PASSTHROUGH_DMTCP_HELPER2(connect,sockfd,serv_addr,addrlen);
}

extern "C" int bind(int sockfd, const struct sockaddr *my_addr,
                     socklen_t addrlen)
{
  WRAPPER_EXECUTION_DISABLE_CKPT(); // The lock is released inside the macro.
  PASSTHROUGH_DMTCP_HELPER(bind, sockfd, my_addr, addrlen);
}

extern "C" int listen(int sockfd, int backlog)
{
  WRAPPER_EXECUTION_DISABLE_CKPT(); // The lock is released inside the macro.
  PASSTHROUGH_DMTCP_HELPER(listen, sockfd, backlog);
}

extern "C" int accept(int sockfd, struct sockaddr *addr,
                      socklen_t *addrlen)
{
  /* FIXME: accept() is a blocking call that can alter the process state(by
   * creating a new socket-fd). This can cause problems if it happens at a time
   * when some other thread is processing inside a fork() or exec() wrapper.
   * For more details, please look at the comment in
   * dmtcp::DmtcpWorker::wrapperExecutionLockLockExcl().
   *
   * Since it's a blocking call, we cannot grab the actual wrapper-execution
   * lock here.
   */
  DUMMY_WRAPPER_EXECUTION_DISABLE_CKPT();
  if (addr == NULL || addrlen == NULL)
  {
    struct sockaddr_storage tmp_addr;
    socklen_t tmp_len = 0;
    memset(&tmp_addr,0,sizeof(tmp_addr));
    PASSTHROUGH_DMTCP_HELPER(accept, sockfd,(struct sockaddr *) &tmp_addr,
                              &tmp_len);
  }
  else
    PASSTHROUGH_DMTCP_HELPER(accept, sockfd, addr, addrlen);
}

extern "C" int accept4(int sockfd, struct sockaddr *addr,
                         socklen_t *addrlen, int flags)
{
  // FIXME: Look at the comment for accept()
  DUMMY_WRAPPER_EXECUTION_DISABLE_CKPT();
  if (addr == NULL || addrlen == NULL)
  {
    struct sockaddr_storage tmp_addr;
    socklen_t tmp_len = 0;
    memset(&tmp_addr,0,sizeof(tmp_addr));
    PASSTHROUGH_DMTCP_HELPER(accept4, sockfd,(struct sockaddr *) &tmp_addr,
                              &tmp_len, flags);
  }
  else
    PASSTHROUGH_DMTCP_HELPER(accept4, sockfd, addr, addrlen, flags);
}

extern "C" int setsockopt(int sockfd, int level, int optname,
                          const void *optval, socklen_t optlen)
{
  WRAPPER_EXECUTION_DISABLE_CKPT(); // The lock is released inside the macro.
  PASSTHROUGH_DMTCP_HELPER(setsockopt,sockfd,level,optname,optval,optlen);
}

extern "C" int getsockopt(int sockfd, int level, int optname,
                          void *optval, socklen_t *optlen)
{
  /* We don't want to acquire the lock here as this is not needed. Also,
   * aquiring the lock here might cause a deadlock when this function is called
   * from within connect(). Here is the deadlock situation:
   * User-thread connect():    acquire lock
   * Ckpt-thread ckpt():       Queued on wr-lock
   * User-thread getsockopt(): block on read lock().
   */
  DUMMY_WRAPPER_EXECUTION_DISABLE_CKPT();
  PASSTHROUGH_DMTCP_HELPER(getsockopt,sockfd,level,optname,optval,optlen);
}

extern "C" int socketpair(int d, int type, int protocol, int sv[2])
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  JASSERT(sv != NULL);
  int rv = _real_socketpair(d,type,protocol,sv);
  JTRACE("socketpair()") (sv[0]) (sv[1]);

  dmtcp::TcpConnection *a, *b;

  a = new dmtcp::TcpConnection(d, type, protocol);
  a->onConnect();
  b = new dmtcp::TcpConnection(*a, a->id());
  a->setSocketpairPeer(b->id());
  b->setSocketpairPeer(a->id());

  dmtcp::KernelDeviceToConnection::instance().create(sv[0] , a);
  dmtcp::KernelDeviceToConnection::instance().create(sv[1] , b);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return rv;
}

