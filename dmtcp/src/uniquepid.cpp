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

#include "uniquepid.h"
#include <stdlib.h>
#include <string.h>
#include <string>
#include "constants.h"
#include "jconvert.h"
#include "jfilesystem.h"

static dmtcp::UniquePid& nullProcess()
{ 
    static dmtcp::UniquePid t(0,0,0); 
    return t;
}
static dmtcp::UniquePid& theProcess()
{ 
    static dmtcp::UniquePid t(0,0,0); 
    return t;
}

const dmtcp::UniquePid& dmtcp::UniquePid::ThisProcess()
{
    if(theProcess() == nullProcess())
    {
        theProcess() = dmtcp::UniquePid(::gethostid(),::getpid(),::time(NULL));
        JTRACE("recalculated process UniquePid...")(theProcess());
    }
    
    return theProcess();
}


/*!
    \fn dmtcp::UniquePid::UniquePid()
 */
 dmtcp::UniquePid::UniquePid()
    :_pid(0)
    ,_hostid(0)
{
    memset(&_time,0,sizeof(_time));
}


long  dmtcp::UniquePid::hostid() const
{
  return _hostid;
}


pid_t  dmtcp::UniquePid::pid() const
{
  return _pid;
}


time_t  dmtcp::UniquePid::time() const
{
    return _time;
}


static bool checkpointFilename_initialized = false;
const char* dmtcp::UniquePid::checkpointFilename()
{
    static std::string checkpointFilename_str = "";
    if(!checkpointFilename_initialized)
    {
        checkpointFilename_initialized = true; 
        checkpointFilename_str = "";
        
        const char* dir = getenv("DMTCP_CHECKPOINT_DIR");
        if(dir != NULL)
        {
            checkpointFilename_str += dir;
            checkpointFilename_str += '/';
        }
        
       const UniquePid& thisProc = ThisProcess();
       checkpointFilename_str += CHECKPOINT_FILE_PREFIX;
       
       checkpointFilename_str += jalib::Filesystem::GetProgramName()
            + '_' + jalib::XToString(thisProc.hostid())
            + '_' + jalib::XToString(thisProc.pid())
            + '_' + jalib::XToString(thisProc.time())
            + ".mtcp";
    }
    return checkpointFilename_str.c_str();
}

std::string dmtcp::UniquePid::dmtcpTableFilename()
{
    static int count = 0;
    return "/tmp/dmtcpConTable." + jalib::XToString( getpid() )
          + '_' + jalib::XToString( count++ );
}

std::string dmtcp::UniquePid::dmtcpCheckpointFilename()
{
    static std::string extraTxt(".dmtcp");
    return checkpointFilename() + extraTxt;
}


/*!
    \fn dmtcp::UniquePid::operator<() const
 */
bool dmtcp::UniquePid::operator<(const UniquePid& that) const
{ 
#define TRY_LEQ(param) if(this->param != that.param) return this->param < that.param;
    TRY_LEQ(_hostid);
    TRY_LEQ(_pid);
    TRY_LEQ(_time);
    return false;
}

bool dmtcp::UniquePid::operator==(const UniquePid& that) const
{ 
    return _hostid==that.hostid()
            && _pid==that.pid()
            && _time==that.time();
}

std::ostream& std::operator<< (std::ostream& o,const dmtcp::UniquePid& id)
{
    o << id.hostid() << '-' << id.pid() << '-' << id.time();
    return o;
}


void dmtcp::UniquePid::resetOnFork(const dmtcp::UniquePid& newId)
{
    JTRACE("Explicitly setting process UniquePid")(newId);
    theProcess() = newId;
    checkpointFilename_initialized = false;
}