/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef SYSCALLWRAPPERS_H
#define SYSCALLWRAPPERS_H

#include <sys/types.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

    
int _real_socket(int domain, int type, int protocol);
int _real_connect(int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen);
int _real_bind(int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen);
int _real_listen(int sockfd, int backlog);
int _real_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int _real_setsockopt(int s, int  level,  int  optname,  const  void  *optval,
       socklen_t optlen);
       
       
int _real_fexecve(int fd, char *const argv[], char *const envp[]);
int _real_execve(const char *filename, char *const argv[], char *const envp[]);
int _real_execv(const char *path, char *const argv[]);
int _real_execvp(const char *file, char *const argv[]);
// int _real_execl(const char *path, const char *arg, ...);
// int _real_execlp(const char *file, const char *arg, ...);
// int _real_execle(const char *path, const char *arg, ..., char * const envp[]);

int _real_close(int fd);
int _real_dup(int oldfd);
int _real_dup2(int oldfd, int newfd);

int _real_socketpair(int d, int type, int protocol, int sv[2]);

void _real_openlog(const char *ident, int option, int facility);
void _real_closelog(void);


                
pid_t _real_fork();


void _dmtcp_lock();
void _dmtcp_unlock();

void _dmtcp_remutex_on_fork();


                
#ifdef __cplusplus
}
#endif

#endif
