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

#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include "jassert.h"
#include "jfilesystem.h"
#include "connectionmanager.h"
#include "dmtcpworker.h"
#include "checkpointcoordinator.h"
#include "mtcpinterface.h"
#include "syscallwrappers.h"
#include "jtimer.h"

static void runMtcpRestore(const std::string& file);

using namespace dmtcp;

namespace {
    
class RestoreTarget
{
public:
    RestoreTarget(const std::string& path)
        : _mtcpPath(path)
        , _dmtcpPath(path + ".dmtcp")
    {
        JASSERT(jalib::Filesystem::FileExists(_mtcpPath))(_mtcpPath).Text("missing file");
        JASSERT(jalib::Filesystem::FileExists(_dmtcpPath))(_dmtcpPath).Text("missing file");
        jalib::JBinarySerializeReader rd(_dmtcpPath);
        _conToFd.serialize(rd);
        JTRACE("restore target")(_mtcpPath)(_conToFd.size());
    }
            
    
    void dupAllSockets(SlidingFdTable& slidingFd)
    {
        for(ConnectionToFds::const_iterator i = _conToFd.begin(); i!=_conToFd.end(); ++i)
        {
            if(ConnectionList::Instance()[i->first].conType() != Connection::TCP) continue;
            const std::vector<int>& fds = i->second;
            for(size_t x=0; x<fds.size(); ++x)
            {
                int fd = fds[x];
                slidingFd.freeUpFd( fd );
                int oldFd = slidingFd.getFdFor( i->first );
                JTRACE("restoring fd")(i->first)(oldFd)(fd);
                JASSERT(_real_dup2(oldFd, fd) == fd)(oldFd)(fd);
            }
        }
        slidingFd.closeAll();
    }
    
    void mtcpRestart()
    {
        DmtcpWorker::maskStdErr();
        runMtcpRestore( _mtcpPath );
    }
    
    std::string     _mtcpPath;
    std::string     _dmtcpPath;
    ConnectionToFds _conToFd;
};
    
    
}//namespace

int main(int argc, char** argv)
{
    
    JASSERT(argc >= 2)(argc).Text("usage: dmtcp_restart ckpt_file1 [ckpt_file2 ...]");
    
    std::vector<RestoreTarget> targets;

    for( int i = argc-1; i>0; --i)
    {
        if(targets.size()>0 && targets.back()._dmtcpPath == argv[i])
            continue;
        
        targets.push_back(RestoreTarget(argv[i]));
    }
    
    
    SlidingFdTable slidingFd;
    ConnectionToFds conToFd;
    
    ConnectionList& connections = ConnectionList::Instance();
    for(ConnectionList::iterator i = connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
        conToFd[i->first].push_back(slidingFd.getFdFor( i->first ));
        JTRACE("will restore")(i->first)(conToFd[i->first].back());
    }
    
    DmtcpWorker worker(false);
    CheckpointCoordinator ckptCoord(conToFd);
    
    worker.restoreSockets( ckptCoord );
    
    
    for(size_t i=0; i<targets.size(); ++i)
    {
        if(i+1 < targets.size())
        {
            JTRACE("forking...");
            int child = fork();
            JASSERT(child >= 0)(child).Text("fork failed");
            if(child != 0)
            {
                sleep(1);
                continue; 
            }
        }
        
        targets[i].dupAllSockets(slidingFd);
        targets[i].mtcpRestart();
    }
    
    
    JASSERT(false).Text("unreachable");
    return -1;
}

static void runMtcpRestore(const std::string& file)
{
    static std::string mtcprestore = jalib::Filesystem::FindHelperUtility( "mtcp_restore" );
    
    char* newArgs[] = 
    {
        (char*)mtcprestore.c_str(),
        (char*)file.c_str(),
        NULL
    };
    
    JTRACE("launching mtcp_restore")(newArgs[1]);
    
    execvp(newArgs[0], newArgs);
    JASSERT(false)(newArgs[0])(newArgs[1])(JASSERT_ERRNO).Text("exec() failed");
}


void dmtcp::initializeMtcpEngine(){
    JASSERT("false").Text("should not be called in dmtcp_restart");}
