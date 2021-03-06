#include <sys/types.h>
#include "jalloc.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "pidwrappers.h"
#include "virtualpidtable.h"
#include "dmtcpplugin.h"
#include "protectedfds.h"

using namespace dmtcp;

extern "C"
pid_t dmtcp_real_to_virtual_pid(pid_t realPid)
{
  return REAL_TO_VIRTUAL_PID(realPid);
}

extern "C"
pid_t dmtcp_virtual_to_real_pid(pid_t virtualPid)
{
  return VIRTUAL_TO_REAL_PID(virtualPid);
}

//static void pidVirt_pthread_atfork_parent()
//{
//  dmtcp::VirtualPidTable::instance().insert(child_pid, child);
//}
//
//static void pidVirt_pthread_atfork_child()
//{
//  pidVirt_resetOnFork
//}

void pidVirt_Init(DmtcpEventData_t *data)
{
//   if ( getenv( ENV_VAR_ROOT_PROCESS ) != NULL ) {
//     JTRACE("Root of processes tree");
//     dmtcp::VirtualPidTable::instance().setRootOfProcessTree();
//     _dmtcp_unsetenv(ENV_VAR_ROOT_PROCESS);
//   }
  //pthread_atfork(NULL, pidVirt_pthread_atfork_parent, pidVirt_pthread_atfork_child);
}

dmtcp::string pidVirt_PidTableFilename()
{
  static int count = 0;
  dmtcp::ostringstream os;

  os << dmtcp_get_tmpdir() << "/dmtcpPidTable." << dmtcp_get_uniquepid_str()
     << '_' << jalib::XToString ( count++ );
  return os.str();
}

void pidVirt_ResetOnFork(DmtcpEventData_t *data)
{
  dmtcp::VirtualPidTable::instance().resetOnFork();
}

void pidVirt_PrepareForExec(DmtcpEventData_t *data)
{
  JASSERT(data != NULL);
  jalib::JBinarySerializeWriterRaw wr ("", data->serializerInfo.fd);
  dmtcp::VirtualPidTable::instance().serialize(wr);
}

void pidVirt_PostExec(DmtcpEventData_t *data)
{
  JASSERT(data != NULL);
  jalib::JBinarySerializeReaderRaw rd ("", data->serializerInfo.fd);
  dmtcp::VirtualPidTable::instance().serialize(rd);
  dmtcp::VirtualPidTable::instance().refresh();
}

void pidVirt_PostRestart(DmtcpEventData_t *data)
{
  if ( jalib::Filesystem::GetProgramName() == "screen" )
    send_sigwinch = 1;
  // With hardstatus (bottom status line), screen process has diff. size window
  // Must send SIGWINCH to adjust it.
  // MTCP will send SIGWINCH to process on restart.  This will force 'screen'
  // to execute ioctl wrapper.  The wrapper will report a changed winsize,
  // so that 'screen' must re-initialize the screen (scrolling regions, etc.).
  // The wrapper will also send a second SIGWINCH.  Then 'screen' will
  // call ioctl and get the correct window size and resize again.
  // We can't just send two SIGWINCH's now, since window size has not
  // changed yet, and 'screen' will assume that there's nothing to do.

  dmtcp::VirtualPidTable::instance().writeMapsToFile(PROTECTED_PIDMAP_FD);
}

void pidVirt_PostRestartRefill(DmtcpEventData_t *data)
{
  dmtcp::VirtualPidTable::instance().readMapsFromFile(PROTECTED_PIDMAP_FD);
}

void pidVirt_ThreadExit(DmtcpEventData_t *data)
{
  /* This thread has finished its execution, do some cleanup on our part.
   *  erasing the original_tid entry from virtualpidtable
   *  FIXME: What if the process gets checkpointed after erase() but before the
   *  thread actually exits?
   */
  pid_t tid = gettid();
  dmtcp::VirtualPidTable::instance().erase(tid);
}

extern "C" void dmtcp_process_event(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_RESET_ON_FORK:
      pidVirt_ResetOnFork(data);
      break;

    case DMTCP_EVENT_PRE_EXEC:
      pidVirt_PrepareForExec(data);
      break;

    case DMTCP_EVENT_POST_EXEC:
      pidVirt_PostExec(data);
      break;

    case DMTCP_EVENT_POST_RESTART:
      pidVirt_PostRestart(data);
      break;

    case DMTCP_EVENT_REFILL:
      if (data->refillInfo.isRestart) {
        pidVirt_PostRestartRefill(data);
      }
      break;

    case DMTCP_EVENT_PTHREAD_RETURN:
    case DMTCP_EVENT_PTHREAD_EXIT:
      pidVirt_ThreadExit(data);
      break;

    default:
      break;
  }

  NEXT_DMTCP_PROCESS_EVENT(event, data);
  return;
}
