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

#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include <ctype.h>
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include "constants.h"
#include "dmtcpmessagetypes.h"
#include "syscallwrappers.h"
#include "coordinatorapi.h"
#include "util.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/personality.h>
#include <string.h>

#define BINARY_NAME "dmtcp_checkpoint"

int testMatlab(const char *filename);
int testJava(char **argv);
bool testSetuid(const char *filename);
void testStaticallyLinked(const char *filename);
bool testScreen(char **argv, char ***newArgv);

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char* theUsage =
  "USAGE: \n"
  "  dmtcp_checkpoint [OPTIONS] <command> [args...]\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --gzip, --no-gzip, (environment variable DMTCP_GZIP=[01]):\n"
  "      Enable/disable compression of checkpoint images (default: 1)\n"
  "      WARNING:  gzip adds seconds.  Without gzip, ckpt is often < 1 s\n"
#ifdef HBICT_DELTACOMP
  "  --hbict, --no-hbict, (environment variable DMTCP_HBICT=[01]):\n"
  "      Enable/disable compression of checkpoint images (default: 1)\n"
#endif
  "  --prefix <arg>:\n"
  "      Prefix where DMTCP is installed on remote nodes.\n"
  "  --ckptdir, -c, (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "      Directory to store checkpoint images (default: ./)\n"
  "  --tmpdir, -t, (environment variable DMTCP_TMPDIR):\n"
  "      Directory to store temporary files \n"
  "        (default: $TMDPIR/dmtcp-$USER@$HOST or /tmp/dmtcp-$USER@$HOST)\n"
  "  --join, -j:\n"
  "      Join an existing coordinator, raise error if one doesn't already exist\n"
  "  --new, -n:\n"
  "      Create a new coordinator, raise error if one already exists\n"
  "  --new-coordinator:\n"
  "      Create a new coordinator even if one already exists\n"
  "  --batch, -b:\n"
  "      Enable batch mode i.e. start the coordinator on the same node on\n"
  "        a randomly assigned port (if no port is specified by --port)\n"
  "  --no-coordinator:\n"
  "      Execute the process in stand-alone coordinator-less mode.\n"
  "        Use dmtcp_command or --interval to request checkpoints.\n"
  "  --interval, -i, (environment variable DMTCP_CHECKPOINT_INTERVAL):\n"
  "      Time in seconds between automatic checkpoints.\n"
  "      0 implies never (manual ckpt only); if not set and no env var,\n"
  "        use default value set in dmtcp_coordinator or dmtcp_command.\n"
  "      Not allowed if --join is specified\n"
  "      --batch implies -i 3600, unless otherwise specified.\n"
  "  --no-check:\n"
  "      Skip check for valid coordinator and never start one automatically\n"
  "  --checkpoint-open-files:\n"
  "      Checkpoint open files and restore old working dir. (Default: do neither)\n"
  "  --mtcp-checkpoint-signal:\n"
  "      Signal number used internally by MTCP for checkpointing (default: 12)\n"
  "  --with-plugin (environment variable DMTCP_PLUGIN):\n"
  "      Colon-separated list of DMTCP plugins to be preloaded with DMTCP.\n"
  "      (Absolute pathnames are required.)\n"
  "  --quiet, -q, (or set environment variable DMTCP_QUIET = 0, 1, or 2):\n"
  "      Skip banner and NOTE messages; if given twice, also skip WARNINGs\n"
  "  --help:\n"
  "      Print this message and exit.\n"
  "  --version:\n"
  "      Print version information and exit.\n"
  "\n"
  "See " PACKAGE_URL " for more information.\n"
;

// FIXME:  The warnings below should be collected into a single function,
//          and also called after a user exec(), not just in dmtcp_checkpoint.
// static const char* theExecFailedMsg =
//   "ERROR: Failed to exec(\"%s\"): %s\n"
//   "Perhaps it is not in your $PATH?\n"
//   "See `dmtcp_checkpoint --help` for usage.\n"
// ;

static dmtcp::string _stderrProcPath()
{
  return "/proc/" + jalib::XToString(getpid()) + "/fd/"
         + jalib::XToString(fileno(stderr));
}

static bool isSSHSlave=false;
static bool autoStartCoordinator=true;
static bool checkpointOpenFiles=false;
static dmtcp::CoordinatorAPI::CoordinatorMode allowedModes = dmtcp::CoordinatorAPI::COORD_ANY;

//shift args
#define shift argc--,argv++
static void processArgs(int *orig_argc, char ***orig_argv)
{
  char argc = *orig_argc;
  char **argv = *orig_argv;

  if (argc == 1) {
    JASSERT_STDERR << DMTCP_VERSION_AND_COPYRIGHT_INFO;
    JASSERT_STDERR << "(For help:  " << argv[0] << " --help)\n\n";
    exit(DMTCP_FAIL_RC);
  }

  //process args
  shift;
  while (true) {
    dmtcp::string s = argc>0 ? argv[0] : "--help";
    if ((s=="--help") && argc==1) {
      JASSERT_STDERR << theUsage;
      exit(DMTCP_FAIL_RC);
    } else if ((s=="--version") && argc==1) {
      JASSERT_STDERR << DMTCP_VERSION_AND_COPYRIGHT_INFO;
      exit(DMTCP_FAIL_RC);
    } else if (s=="--ssh-slave") {
      isSSHSlave = true;
      shift;
    } else if (s == "--no-check") {
      autoStartCoordinator = false;
      shift;
    } else if (s == "-j" || s == "--join") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_JOIN;
      shift;
    } else if (s == "--gzip") {
      setenv(ENV_VAR_COMPRESSION, "1", 1);
      shift;
    } else if (s == "--no-gzip") {
      setenv(ENV_VAR_COMPRESSION, "0", 1);
      shift;
    }
#ifdef HBICT_DELTACOMP
    else if (s == "--hbict") {
      setenv(ENV_VAR_DELTACOMPRESSION, "1", 1);
      shift;
    } else if (s == "--no-hbict") {
      setenv(ENV_VAR_DELTACOMPRESSION, "0", 1);
      shift;
    }
#endif
    else if (s == "-n" || s == "--new") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_NEW;
      shift;
    } else if (s == "--new-coordinator") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_FORCE_NEW;
      shift;
    } else if (s == "-b" || s == "--batch") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_BATCH;
      shift;
    } else if (s == "--no-coordinator") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_NONE;
      shift;
    } else if (s == "-i" || s == "--interval" ||
             (s.c_str()[0] == '-' && s.c_str()[1] == 'i' &&
              isdigit(s.c_str()[2]) ) ) {
      if (isdigit(s.c_str()[2])) { // if -i5, for example
        setenv(ENV_VAR_CKPT_INTR, s.c_str()+2, 1);
        shift;
      } else { // else -i 5
        setenv(ENV_VAR_CKPT_INTR, argv[1], 1);
        shift; shift;
      }
    } else if (argc>1 && (s == "-h" || s == "--host")) {
      setenv(ENV_VAR_NAME_HOST, argv[1], 1);
      shift; shift;
    } else if (argc>1 && (s == "-p" || s == "--port")) {
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    } else if (argc>1 && (s == "--prefix")) {
      setenv(ENV_VAR_PREFIX_PATH, argv[1], 1);
      shift; shift;
    } else if (argc>1 && (s == "-c" || s == "--ckptdir")) {
      setenv(ENV_VAR_CHECKPOINT_DIR, argv[1], 1);
      shift; shift;
    } else if (argc>1 && (s == "-t" || s == "--tmpdir")) {
      setenv(ENV_VAR_TMPDIR, argv[1], 1);
      shift; shift;
    } else if (argc>1 && s == "--mtcp-checkpoint-signal") {
      setenv(ENV_VAR_SIGCKPT, argv[1], 1);
      shift; shift;
    } else if (s == "--checkpoint-open-files") {
      checkpointOpenFiles = true;
      shift;
    } else if (s == "--with-plugin") {
      setenv(ENV_VAR_PLUGIN, argv[1], 1);
      shift; shift;
    } else if (s == "--with-module") { /* TEMPORARILY SUPPORT BACKWARD COMPAT.*/
      setenv(ENV_VAR_PLUGIN, argv[1], 1);
      shift; shift;
    } else if (s == "-q" || s == "--quiet") {
      *getenv(ENV_VAR_QUIET) = *getenv(ENV_VAR_QUIET) + 1;
      // Just in case a non-standard version of setenv is being used:
      setenv(ENV_VAR_QUIET, getenv(ENV_VAR_QUIET), 1);
      shift;
    } else if ( (s.length()>2 && s.substr(0,2)=="--") ||
              (s.length()>1 && s.substr(0,1)=="-" ) ) {
      JASSERT_STDERR << "Invalid Argument\n";
      JASSERT_STDERR << theUsage;
      exit(DMTCP_FAIL_RC);
    } else if (argc>1 && s=="--") {
      shift;
      break;
    } else {
      break;
    }
  }
  *orig_argv = argv;
}

int main ( int argc, char** argv )
{
  initializeJalib();

  if (! getenv(ENV_VAR_QUIET))
    setenv(ENV_VAR_QUIET, "0", 0);

  processArgs(&argc, &argv);

  // If --ssh-slave and --prefix both are present, verify that the prefix-dir
  // of this binary (dmtcp_checkpoint) is same as the one provided with
  // --prefix
  if (isSSHSlave && getenv(ENV_VAR_PREFIX_PATH) != NULL) {
    const char *str = getenv(ENV_VAR_PREFIX_PATH);
    dmtcp::string prefixDir = jalib::Filesystem::ResolveSymlink(str);
    dmtcp::string programPrefixDir =
      jalib::Filesystem::DirName(jalib::Filesystem::GetProgramDir());
    JASSERT(prefixDir == programPrefixDir)
      (prefixDir) (programPrefixDir);
  }

  dmtcp::UniquePid::setTmpDir(getenv(ENV_VAR_TMPDIR));
  dmtcp::UniquePid::ThisProcess(true);
  dmtcp::Util::initializeLogFile();

#ifdef FORKED_CHECKPOINTING
  /* When this is robust, add --forked-checkpointing option on command-line,
   * with #ifdef FORKED_CHECKPOINTING around the option, change default of
   * configure.ac, dmtcp/configure.ac, to enable, and change them
   * from enable-forked... to disable-...
   */
  setenv(ENV_VAR_FORKED_CKPT, "1", 1);
#endif

  if (jassert_quiet == 0)
    JASSERT_STDERR << DMTCP_BANNER;

  // This code will go away when zero-mapped pages are implemented in MTCP.
  struct rlimit rlim;
  getrlimit(RLIMIT_STACK, &rlim);
  if (rlim.rlim_cur > 256*1024*1024 && rlim.rlim_cur != RLIM_INFINITY)
    JASSERT_STDERR <<
      "*** WARNING:  RLIMIT_STACK > 1/4 GB.  This causes each thread to"
      "\n***  receive a 1/4 GB stack segment.  Checkpoint/restart will be slow,"
      "\n***  and will potentially break if many threads are created."
      "\n*** Suggest setting (sh/bash):  ulimit -s 10000"
      "\n***                (csh/tcsh):  limit stacksize 10000"
      "\n*** prior to using DMTCP.  (This will be fixed in the future, when"
      "\n*** DMTCP supports restoring zero-mapped pages.)\n\n\n" ;
  // Remove this when zero-mapped pages are supported.  For segments with
  // no file backing:  Start with 4096 (page) offset and keep doubling offset
  // until finding region of memory segment with many zeroes.
  // Then mark as CS_ZERO_PAGES in MTCP instead of CS_RESTORE (or mark
  // entire segment as CS_ZERO_PAGES and then overwrite with CS_RESTORE
  // region for portion to be read back from checkpoint image.
  // For CS_ZERO_PAGES region, mmap // on restart, but don't write in zeroes.
  // Also, after checkpointing segment, munmap zero pages, and mmap them again.
  // Don't try to find all pages.  The above strategy may increase
  // the non-zero-mapped mapped pages to no more than double the actual
  // non-zero region (assuming that the zero-mapped pages are contiguous).
  // - Gene

  testMatlab(argv[0]);
  testJava(argv);  // Warn that -Xmx flag needed to limit virtual memory size

  // If dmtcphijack.so is in standard search path and _also_ has setgid access,
  //   then LD_PRELOAD will work.
  // Otherwise, it will only work if the application does not use setuid and
  //   setgid access.  So, we test //   if the application does not use
  //   setuid/setgid.  (See 'man ld.so')
  // FIXME:  ALSO DO THIS FOR execwrappers.cpp:dmtcpPrepareForExec()
  //   Should pass dmtcphijack.so path, and let testSetuid determine
  //     if setgid is set for it.  If so, no problem:  continue.
  //   If not, call testScreen() and adapt 'screen' to run using
  //     Util::patchArgvIfSetuid(argv[0], argv, &newArgv) (which shouldn't
  //     will just modify argv[0] to point to /tmp/dmtcp-USER@HOST/screen
  //     and other modifications:  doesn't need newArgv).
  //   If it's not 'screen' and if no setgid for dmtcphijack.so, then testSetuid
  //    should issue the warning, unset our LD_PRELOAD, and hope for the best.
  //    A program like /usr/libexec/utempter/utempter (Fedora path)
  //    is short-lived and can be safely run.  Ideally, we should
  //    disable checkpoints while utempter is running, and enable checkpoints
  //    when utempter finishes.  See possible model at
  //    execwrappers.cpp:execLibProcessAndExit(), since the same applies
  //    to running /lib/libXXX.so for running libraries as executables.
  if (testSetuid(argv[0])) {
    char **newArgv;
    // THIS NEXT LINE IS DANGEROUS.  MOST setuid PROGRAMS CAN'T RUN UNPRIVILEGED
    dmtcp::Util::patchArgvIfSetuid(argv[0], argv, &newArgv);
    argv = newArgv;
  };

  if (argc > 0) {
    JTRACE("dmtcp_checkpoint starting new program:")(argv[0]);
  }

  //set up CHECKPOINT_DIR
  if(getenv(ENV_VAR_CHECKPOINT_DIR) == NULL){
    const char* ckptDir = get_current_dir_name();
    if(ckptDir != NULL ){
      //copy to private buffer
      static dmtcp::string _buf = ckptDir;
      ckptDir = _buf.c_str();
    }else{
      ckptDir=".";
    }
    setenv ( ENV_VAR_CHECKPOINT_DIR, ckptDir, 0 );
    JTRACE("setting " ENV_VAR_CHECKPOINT_DIR)(ckptDir);
  }

  dmtcp::string stderrDevice = jalib::Filesystem::ResolveSymlink ( _stderrProcPath() );

  //TODO:
  // When stderr is a pseudo terminal for IPC between parent/child processes,
  //  this logic fails and JASSERT may write data to FD 2 (stderr).
  // This will cause problems in programs that use FD 2 (stderr) for
  //  algorithmic things ...
  if ( stderrDevice.length() > 0
          && jalib::Filesystem::FileExists ( stderrDevice ) )
    setenv ( ENV_VAR_STDERR_PATH,stderrDevice.c_str(), 0 );
  else// if( isSSHSlave )
    setenv ( ENV_VAR_STDERR_PATH, "/dev/null", 0 );

  if ( getenv(ENV_VAR_SIGCKPT) != NULL )
    setenv ( "MTCP_SIGCKPT", getenv(ENV_VAR_SIGCKPT), 1);
  else
    unsetenv("MTCP_SIGCKPT");

  if ( checkpointOpenFiles )
    setenv( ENV_VAR_CKPT_OPEN_FILES, "1", 0 );
  else
    unsetenv( ENV_VAR_CKPT_OPEN_FILES);

#ifdef PID_VIRTUALIZATION
  setenv( ENV_VAR_ROOT_PROCESS, "1", 1 );
#endif

  bool isElf, is32bitElf;
  if  (dmtcp::Util::elfType(argv[0], &isElf, &is32bitElf) == -1) {
    // Couldn't read argv_buf
    // FIXME:  This could have been a symbolic link.  Don't issue an error,
    //         unless we're sure that the executable is not readable.
    JASSERT_STDERR <<
      "*** ERROR:  Executable to run w/ DMTCP appears not to be readable,\n"
      "***         or no such executable in path.\n\n"
      << argv[0] << "\n";
    exit(DMTCP_FAIL_RC);
  } else {
#if defined(__x86_64__) && !defined(CONFIG_M32)
    if (is32bitElf)
      JASSERT_STDERR << "*** ERROR:  You appear to be checkpointing "
        << "a 32-bit target under 64-bit Linux.\n"
        << "***  If this fails, then please try re-configuring DMTCP:\n"
        << "***  configure --enable-m32 ; make clean ; make\n\n";
#endif

    testStaticallyLinked(argv[0]);
  }

  // UNSET DISPLAY environment variable.
  unsetenv("DISPLAY");

// FIXME:  Unify this code with code prior to execvp in execwrappers.cpp
//   Can use argument to dmtcpPrepareForExec() or getenv("DMTCP_...")
//   from DmtcpWorker constructor, to distinguish the two cases.
  dmtcp::Util::adjustRlimitStack();

  // FIXME: This call should be moved closer to call to execvp().
  dmtcp::Util::prepareDlsymWrapper();

  if (autoStartCoordinator) {
     dmtcp::CoordinatorAPI::startCoordinatorIfNeeded(allowedModes);
  }
  dmtcp::CoordinatorAPI coordinatorAPI;
  pid_t virtualPid = coordinatorAPI.getVirtualPidFromCoordinator();
  if (virtualPid != -1) {
    JTRACE("Got virtual pid from coordinator") (virtualPid);
    dmtcp::Util::setVirtualPidEnvVar(virtualPid, getppid());
  }

  // preloadLibs are to set LD_PRELOAD:
  //   LD_PRELOAD=PLUGIN_LIBS:UTILITY_DIR/dmtcphijack.so:R_LIBSR_UTILITY_DIR/
  dmtcp::string preloadLibs = "";
  // FIXME:  If the colon-separated elements of ENV_VAR_PLUGIN are not
  //     absolute pathnames, then they must be expanded to absolute pathnames.
  //     Warn user if an absolute pathname is not valid.
  if ( getenv(ENV_VAR_PLUGIN) != NULL ) {
    preloadLibs += getenv(ENV_VAR_PLUGIN);
    preloadLibs += ":";
  }
  // FindHelperUtiltiy requires ENV_VAR_UTILITY_DIR to be set
  dmtcp::string searchDir = jalib::Filesystem::GetProgramDir();
  setenv ( ENV_VAR_UTILITY_DIR, searchDir.c_str(), 0 );

#ifdef PTRACE
  preloadLibs += jalib::Filesystem::FindHelperUtility ( "ptracehijack.so" );
  preloadLibs += ":";
#endif

  preloadLibs += jalib::Filesystem::FindHelperUtility ( "dmtcphijack.so" );

#ifdef PID_VIRTUALIZATION
  preloadLibs += ":";
  preloadLibs += jalib::Filesystem::FindHelperUtility ( "pidvirt.so" );
#endif

  const char *ldLibPath = getenv("LD_LIBRARY_PATH");
  dmtcp::string libPath;
  if (ldLibPath != NULL) {
    libPath = ldLibPath;
  }
  libPath += ":" + jalib::Filesystem::DirName(
               jalib::Filesystem::FindHelperUtility("libmtcp.so.1"));
  JASSERT(!libPath.empty());
  setenv("LD_LIBRARY_PATH", libPath.c_str(), 1);

  setenv(ENV_VAR_HIJACK_LIBS, preloadLibs.c_str(), 1);

  // If dmtcp_checkpoint was called with user LD_PRELOAD, and if
  //   if dmtcp_checkpoint survived the experience, then pass it back to user.
  if (getenv("LD_PRELOAD"))
    preloadLibs = preloadLibs + ":" + getenv("LD_PRELOAD");

  setenv ( "LD_PRELOAD", preloadLibs.c_str(), 1 );
  JTRACE("getting value of LD_PRELOAD")(getenv("LD_PRELOAD"));

  //run the user program
  char **newArgv = NULL;
  if (testScreen(argv, &newArgv))
    execvp ( newArgv[0], newArgv );
  else
    execvp ( argv[0], argv );

  //should be unreachable
  JASSERT_STDERR <<
    "ERROR: Failed to exec(\"" << argv[0] << "\"): " << JASSERT_ERRNO << "\n"
    << "Perhaps it is not in your $PATH?\n"
    << "See `dmtcp_checkpoint --help` for usage.\n";
  //fprintf(stderr, theExecFailedMsg, argv[0], JASSERT_ERRNO);

  return -1;
}

int testMatlab(const char *filename) {
#ifdef __GNUC__
# if __GNUC__ == 4 && __GNUC_MINOR__ > 1
  static const char* theMatlabWarning =
    "\n**** WARNING:  Earlier Matlab releases (e.g. release 7.4) use an\n"
    "****  older glibc.  Later releases (e.g. release 7.9) have no problem.\n"
    "****  \n"
    "****  If you are using an _earlier_ Matlab, please re-compile DMTCP/MTCP\n"
    "****  with gcc-4.1 and g++-4.1\n"
    "**** env CC=gcc-4.1 CXX=g++-4.1 ./configure\n"
    "**** [ Also modify mtcp/Makefile to:  CC=gcc-4.1 ]\n"
    "**** [ Next, you may need an alternative Java JVM (see QUICK-START) ]\n"
    "**** [ Finally, run as:   dmtcp_checkpoint matlab -nodisplay ]\n"
    "**** [   (DMTCP does not yet checkpoint X-Windows applications.) ]\n"
    "**** [ You may see \"Not checkpointing libc-2.7.so\".  This is normal. ]\n"
    "****   (Assuming you have done the above, Will now continue"
	    " executing.)\n\n" ;

  // FIXME:  should expand filename and "matlab" before checking
  if ( strcmp(filename, "matlab") == 0 && getenv(ENV_VAR_QUIET) == NULL) {
    JASSERT_STDERR << theMatlabWarning;
    return -1;
  }
# endif
#endif
  return 0;
}

// FIXME:  Remove this when DMTCP supports zero-mapped pages
int testJava(char **argv) {
  static const char* theJavaWarning =
    "\n**** WARNING:  Sun/Oracle Java claims a large amount of memory\n"
    "****  for its heap on startup.  As of DMTCP version 1.2.4, DMTCP _does_\n"
    "****  handle zero-mapped virtual memory, but it may take up to a\n"
    "****  minute.  This will be fixed to be much faster in a future\n"
    "****  version of DMTCP.  In the meantime, if your Java supports it,\n"
    "****  use the -Xmx flag for a smaller heap:  e.g.  java -Xmx64M javaApp\n"
    "****  (Invoke dmtcp_checkpoint with --quiet to avoid this msg.)\n\n" ;

  if (getenv(ENV_VAR_QUIET) != NULL
      && strcmp(getenv(ENV_VAR_QUIET), "0") != 0)
    return 0;
  if ( strcmp(argv[0], "java") == 0 ) {
    while (*(++argv) != NULL)
      if (strncmp(*argv, "-Xmx", sizeof("-Xmx")-1) == 0)
        return 0; // The user called java with -Xmx.  No need for warning.
  }

  // If user has more than 4 GB of RAM, warn them that -Xmx is faster.
  int fd;
  char buf[100];
  static const char *meminfoPrefix = "MemTotal:       ";
  if ( (fd = open("/proc/meminfo", O_RDONLY)) != -1 &&
    read(fd, buf, sizeof(meminfoPrefix) + 16) == sizeof(meminfoPrefix) + 16 &&
    strncmp(buf, meminfoPrefix, sizeof(meminfoPrefix)+1) == 0 &&
    atol(buf + sizeof(meminfoPrefix)) > 17000000) /* units of kB : mem > 4 GB */
      JASSERT_STDERR << theJavaWarning;
  if (fd != -1)
    close(fd);
  return -1;
}

bool testSetuid(const char *filename)
{
  if (dmtcp::Util::isSetuid(filename) &&
      strcmp(filename, "screen") != 0 && strstr(filename, "/screen") == NULL) {

    static const char* theSetuidWarning =
      "\n**** WARNING:  This process has the setuid or setgid bit set.  This is\n"
      "***  incompatible with the use by DMTCP of LD_PRELOAD.  The process\n"
      "***  will not be checkpointed by DMTCP.  Continuing and hoping\n"
      "***  for the best.  For some programs, you may wish to\n"
      "***  compile your own private copy, without using setuid permission.\n\n" ;

    JASSERT_STDERR << theSetuidWarning;
    sleep(3);
    return true;
  }
  return false;
}

void testStaticallyLinked(const char *pathname) {
  if (dmtcp::Util::isStaticallyLinked(pathname)) {
    JASSERT_STDERR <<
      "*** WARNING:  /lib/ld-linux.so --verify " << pathname << " returns\n"
      << "***  nonzero status.  (Some distros"
         " use /lib64/ld-linux-x86-64.so .)\n"
      << "*** This often means that " << pathname << " is\n"
      << "*** a statically linked target.  If so, you can confirm this with\n"
      << "*** the 'file' command.\n"
      << "***  The standard DMTCP only supports dynamically"
      << " linked executables.\n"
      << "*** If you cannot recompile dynamically, please talk to the"
      << " developers about a\n"
      << "*** custom DMTCP version for statically linked executables.\n"
      << "*** Proceeding for now, and hoping for the best.\n\n";
  }
  return;
}

// Test for 'screen' program, argvPtr is an in- and out- parameter
bool testScreen(char **argv, char ***newArgv)
{
  if (dmtcp::Util::isScreen(argv[0])) {
    dmtcp::Util::setScreenDir();
    dmtcp::Util::patchArgvIfSetuid(argv[0], argv, newArgv);
    return true;
  }
  return false;
}
