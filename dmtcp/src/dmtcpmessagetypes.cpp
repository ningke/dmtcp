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

#include "dmtcpmessagetypes.h"

static dmtcp::WorkerState theState(dmtcp::WorkerState::RUNNING);

dmtcp::WorkerState dmtcp::WorkerState::currentState()
{
  return theState;
}


void dmtcp::WorkerState::setCurrentState(const dmtcp::WorkerState& theValue)
{
  theState = theValue;
}

static dmtcp::UniquePid theDefaultMaster;

void dmtcp::DmtcpMessage::setDefaultMaster(const UniquePid& id){theDefaultMaster = id;}
        
dmtcp::DmtcpMessage::DmtcpMessage(DmtcpMessageType t /*= DMT_NULL*/) 
    :_msgSize(sizeof(DmtcpMessage))
    ,type(t)
    ,from(ConnectionIdentifier::Self())
    ,master(theDefaultMaster)
    ,state(WorkerState::currentState())
    ,restorePid(ConnectionIdentifier::Null())
    ,restoreAddrlen(0)
    ,restorePort(-1)
    ,extraBytes(0)
{ 
//     struct sockaddr_storage _addr;
//         socklen_t _addrlen;
    strncpy(_magicBits,DMTCP_MAGIC_STRING,sizeof(_magicBits)); 
    memset(&params,0,sizeof(params));
    memset(&restoreAddr,0,sizeof(restoreAddr));
}

void dmtcp::DmtcpMessage::assertValid() const
{
    JASSERT(strcmp(DMTCP_MAGIC_STRING,_magicBits) == 0)
        (DMTCP_MAGIC_STRING)(_magicBits)
        .Text("invalid message, perhaps tried to connect to non-dmtcp enabled host");
    JASSERT(_msgSize == sizeof(DmtcpMessage))(_msgSize)(sizeof(DmtcpMessage))
        .Text("invalid message");
}

void dmtcp::DmtcpMessage::poison() { memset(_magicBits,0,sizeof(_magicBits)); } 


dmtcp::WorkerState::eWorkerState dmtcp::WorkerState::value() const
{
  return _state;
}

std::ostream& dmtcp::operator << (std::ostream& o, const dmtcp::WorkerState& s)
{
    o << "WorkerState::";
    switch(s.value())
    {
#define OSHIFTPRINTF(name) case WorkerState::name: o << #name; break; 
        
            OSHIFTPRINTF(UNKOWN)
            OSHIFTPRINTF(RUNNING)
            OSHIFTPRINTF(SUSPENDED)
            OSHIFTPRINTF(DRAINED)
            OSHIFTPRINTF(RESTARTING)
            OSHIFTPRINTF(CHECKPOINTED)
            OSHIFTPRINTF(REFILLED)
        default:
            o << s.value();
    }
    return o;
}