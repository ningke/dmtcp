/****************************************************************************
 *   Copyright (C) 2006-2012 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "constants.h"
#include "syscallwrappers.h"
#include "ckptserializer.h"

static pid_t ext_decomp_pid = -1;

static void close_ckpt_to_read(const int fd)
{
  int status;
  int rc;
  int pid = ext_decomp_pid;
  if (pid != -1) {
    // First close fd to let decompressor know that we want to close it
    while (-1 == (rc = close(fd)) && errno == EINTR);
    JASSERT (rc != -1) ("close:") (JASSERT_ERRNO);
    // Kill the decompressor process
    // 1. Send SIGTERM to give the decompressor a chance to clean up
    JASSERT (kill(pid, SIGTERM) != -1) (JASSERT_ERRNO);
    // 2. Wait 3 seconds for decompressor termination
    rc = 0;
    for (int i = 0; (i < 3000 && rc != pid); i++) {
      struct timespec sleepTime = {0, 1*1000*1000};
      nanosleep(&sleepTime, NULL);
      rc = waitpid(pid, &status, WNOHANG);
    }
    // 3. If the decompressor process still exists
    if (rc != pid) {
      if ((rc = kill(pid, SIGKILL)) == -1 && errno != ESRCH) {
        // process exists but we failed to kill it
        JASSERT (rc != -1)("kill:") (JASSERT_ERRNO);
      } else {
        // process exists and it was SIGKILL'ed. Endless wait for exit.
        while (0 == (rc = waitpid(pid,&status, WNOHANG))) {
          struct timespec sleepTime = {0, 1*1000*1000};
          nanosleep(&sleepTime, NULL);
        }
      }
      JASSERT (rc == pid) ("waitpid:") (JASSERT_ERRNO);
    }
    ext_decomp_pid = -1;
  }
}

// Copied from mtcp/mtcp_restart.c.
#define DMTCP_MAGIC_FIRST 'D'
#define GZIP_FIRST 037
#ifdef HBICT_DELTACOMP
#define HBICT_FIRST 'H'
#endif

char *mtcp_executable_path(char *filename);
static char first_char(const char *filename)
{
  int fd, rc;
  char c;

  fd = open(filename, O_RDONLY);
  JASSERT(fd >= 0) (filename) .Text("ERROR: Cannot open filename");

  rc = _real_read(fd, &c, 1);
  JASSERT(rc == 1) (filename) .Text("ERROR: Error reading from filename");

  close(fd);
  return c;
}

// Copied from mtcp/mtcp_restart.c.
// Let's keep this code close to MTCP code to avoid maintenance problems.
// MTCP code in:  mtcp/mtcp_restart.c:open_ckpt_to_read()
// A previous version tried to replace this with popen, causing a regression:
//   (no call to pclose, and possibility of using a wrong fd).
// Returns fd; sets ext_decomp_pid, if checkpoint was compressed.
static int open_ckpt_to_read(const char *filename)
{
  int fd;
  int fds[2];
  char fc;
  const char *decomp_path;
  const char **decomp_args;
  const char *gzip_path = "gzip";
  static const char * gzip_args[] = { "gzip", "-d", "-", NULL };
#ifdef HBICT_DELTACOMP
  const char *hbict_path = "hbict";
  static const char *hbict_args[] = { "hbict", "-r", NULL };
#endif
  pid_t cpid;

  fc = first_char(filename);
  fd = open(filename, O_RDONLY);
  JASSERT(fd>=0)(filename).Text("Failed to open file.");

  if (fc == DMTCP_MAGIC_FIRST) { /* no compression */
    return fd;
  }
  else if (fc == GZIP_FIRST
#ifdef HBICT_DELTACOMP
           || fc == HBICT_FIRST
#endif
          ) {
    if (fc == GZIP_FIRST) {
      decomp_path = gzip_path;
      decomp_args = gzip_args;
    }
#ifdef HBICT_DELTACOMP
    else {
      decomp_path = hbict_path;
      decomp_args = hbict_args;
    }
#endif

    JASSERT(pipe(fds) != -1) (filename)
      .Text("Cannot create pipe to execute gunzip to decompress ckpt file!");

    cpid = _real_fork();

    JASSERT(cpid != -1)
      .Text("ERROR: Cannot fork to execute gunzip to decompress ckpt file!");
    if (cpid > 0) { /* parent process */
      JTRACE("created child process to uncompress checkpoint file") (cpid);
      ext_decomp_pid = cpid;
      close(fd);
      close(fds[1]);
      return fds[0];
    } else { /* child process */
      JTRACE ( "child process, will exec into external de-compressor");
      fd = dup(dup(dup(fd)));
      fds[1] = dup(fds[1]);
      close(fds[0]);
      JASSERT(fd != -1);
      JASSERT(dup2(fd, STDIN_FILENO) == STDIN_FILENO);
      close(fd);
      JASSERT(dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
      close(fds[1]);
      _real_execvp(decomp_path, (char **)decomp_args);
      JASSERT(decomp_path!=NULL) (decomp_path)
        .Text("Failed to launch gzip.");
      /* should not get here */
      JASSERT(false)
        .Text("Decompression failed!  No restoration will be performed!");
    }
  } else { /* invalid magic number */
    JASSERT(false)
      .Text("ERROR: Invalid magic number in this checkpoint file!");
  }
  return -1;
}

// See comments above for open_ckpt_to_read()
int dmtcp::CkptSerializer::openDmtcpCheckpointFile(const dmtcp::string& path,
                                                   int *offset,
                                                   int skipBytes)
{
  char buf[1024];
  // Function also sets dmtcp::ext_decomp_pid::ConnectionToFds
  int fd = open_ckpt_to_read(path.c_str());
  // The rest of this function is for compatibility with original definition.
  JASSERT(fd >= 0) (path) .Text("Failed to open file.");
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(_real_read(fd, buf, len) == len)(path) .Text("_real_read() failed");
  if (strncmp(buf, DMTCP_FILE_HEADER, len) == 0) {
    JTRACE("opened checkpoint file [uncompressed]")(path);
  } else {
    close_ckpt_to_read(fd);
    fd = open_ckpt_to_read(path.c_str()); /* Re-open from beginning */
    JASSERT(fd >= 0) (path) .Text("Failed to open file.");
  }

  if (offset != NULL) {
    *offset = strlen(DMTCP_FILE_HEADER);
  }

  skipBytes -= strlen(DMTCP_FILE_HEADER);
  if (skipBytes > 0) {
    JASSERT(dmtcp::Util::skipBytes(fd, skipBytes) == skipBytes) (skipBytes);
  }
  return fd;
}

void dmtcp::CkptSerializer::closeDmtcpCheckpointFile(int fd)
{
  JASSERT(fd >= 0);
  close_ckpt_to_read(fd);
}

void dmtcp::CkptSerializer::writeCkptSign(int fd)
{
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(write(fd, DMTCP_FILE_HEADER, len) == len);
}
