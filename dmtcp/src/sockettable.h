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
#ifndef SOCKETTABLE_H
#define SOCKETTABLE_H

#include <sys/types.h>
#include <sys/socket.h>



#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC int dmtcp_on_socket(int ret, int domain, int type, int protocol);
EXTERNC int dmtcp_on_connect(int ret, int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen);
EXTERNC int dmtcp_on_bind(int ret, int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen);
EXTERNC int dmtcp_on_listen(int ret, int sockfd, int backlog);
EXTERNC int dmtcp_on_accept(int ret, int sockfd, struct sockaddr *addr, socklen_t *addrlen);
EXTERNC int dmtcp_on_error(int ret, int sockfd, const char* fname);
EXTERNC int dmtcp_on_setsockopt(int ret, int sockfd, int  level,  int  optname,  const  void  *optval, socklen_t optlen);



#ifdef __cplusplus

#include <map>
#include <vector>
#include "dmtcpmessagetypes.h"
#include "jbuffer.h"

namespace dmtcp
{
//     class SocketEntry
//     {
//     public: 
//         enum SocketState {
//             T_INVALID,
//             T_ERROR,
//             T_CREATED,
//             T_CONNECT,
//             T_BIND,
//             T_LISTEN,
//             T_ACCEPT
//         };
//         
//         const struct sockaddr* addr() const;
//         socklen_t addrlen() const;
//         void setAddr(const struct sockaddr* theValue, socklen_t len);
//         
//         void setState(const SocketState& theValue);
//         SocketState state() const;
//         
//         
//         void setProtocol(const int& theValue);
//         int protocol() const;
//         
//         
//         void setDomain(const int& theValue);
//         int domain() const;
//         
//         
//         void setBacklog(const int& theValue);
//         int backlog() const;
//      
//         //constructor
//         SocketEntry();
// 
// 	void setType(const int& theValue);
// 	
// 
// 	int type() const;
// 
// 	void setRemoteId(const UniquePidConId& theValue);
//         void changeRemoteId(const UniquePid& theValue);
// 	
// 
// 	const UniquePidConId& remoteId() const;
// 
// 	void setSockfd(const int& theValue);
// 	
// 
// 	int sockfd() const;
// 
// 	void setNeedRestore(bool theValue);
// 
// 	bool needRestore() const;
//         
//         void setIsLoopback(bool theValue);
// 
// 	bool isLoopback() const;
// 	
//         void addSetsockopt(int level, int option, const char* value, int len);
//         void restoreOptions();
//             
//         bool isStillAlive() const;
//         
//         
//     private:
//         int _sockfd;
//         SocketState _state;
//         int _domain;
//         int _type;
//         int _protocol;
//         int _backlog;
//         socklen_t               _addrlen;
//         struct sockaddr_storage _addr;
//         UniquePidConId             _remoteId;
//         bool   _needRestore;
//         bool   _isLoopback;
//         std::map< int, std::map< int, jalib::JBuffer > > _options; // _options[level][option] = value
//     };
// 
//     class SocketTable {
//     public:
//         typedef std::vector<SocketEntry>::iterator iterator;
//         
//         static SocketTable& Instance();
//         SocketEntry& operator[] (int sockfd);
//         static SocketEntry& LookupByFd(int sockfd){ return Instance()[sockfd]; }
//         
//         iterator begin() {return _entries.begin();}
//         iterator end() {return _entries.end();}
//         
//         void onForkUpdate(const dmtcp::UniquePid& parent, const dmtcp::UniquePid& child);
//         
//         void resetFd(int fd);
//         
//     protected:
//         SocketTable();
//     private:
//         //entries by sockfd
//         std::vector<SocketEntry> _entries;
//     };
// 
// 	
}

#endif


#endif
