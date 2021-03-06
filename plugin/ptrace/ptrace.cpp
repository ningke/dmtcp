/*****************************************************************************
 *   Copyright (C) 2008-2012 by Ana-Maria Visan, Kapil Arya, and             *
 *                                                            Gene Cooperman *
 *   amvisan@cs.neu.edu, kapil@cs.neu.edu, and gene@ccs.neu.edu              *
 *                                                                           *
 *   This file is part of the PTRACE plugin of DMTCP (DMTCP:mtcp).           *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:plugin/ptrace is distributed in the hope that it will be useful,   *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include "jalloc.h"
#include "jassert.h"
#include "ptrace.h"
#include "ptraceinfo.h"
#include "dmtcpplugin.h"
#include "util.h"

static int originalStartup = 1;

void ptraceInit()
{
  dmtcp::PtraceInfo::instance().createSharedFile();
  dmtcp::PtraceInfo::instance().mapSharedFile();
}

void ptraceWaitForSuspendMsg(DmtcpEventData_t *data)
{
  dmtcp::PtraceInfo::instance().markAsCkptThread();
  if (!originalStartup) {
    dmtcp::PtraceInfo::instance().waitForSuperiorAttach();
  } else {
    originalStartup = 0;
  }
}

void ptraceProcessResumeUserThread(DmtcpEventData_t *data)
{
  ptrace_process_resume_user_thread(data->resumeUserThreadInfo.isRestart);
}

extern "C" void dmtcp_process_event(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      ptraceInit();
      break;

    case DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG:
      ptraceWaitForSuspendMsg(data);
      break;

    case DMTCP_EVENT_PRE_SUSPEND_USER_THREAD:
      ptrace_process_pre_suspend_user_thread();
      break;

    case DMTCP_EVENT_RESUME_USER_THREAD:
      ptraceProcessResumeUserThread(data);
      break;

    case DMTCP_EVENT_RESET_ON_FORK:
      originalStartup = 1;
      break;

    default:
      break;
  }

  NEXT_DMTCP_PROCESS_EVENT(event, data);
  return;
}
