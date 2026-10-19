/* XMRigCC
 * Copyright 2017-     BenDr0id    <https://github.com/BenDr0id>, <ben@graef.in>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <thread>
#include <vector>
#include "base/kernel/BuildInfo.h"
#include "version.h"

#include "XMRigd.h"

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN  /* avoid including junk */
#include <windows.h>
#include <signal.h>
#else
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#endif

#ifndef MINER_EXECUTABLE_NAME
#define MINER_EXECUTABLE_NAME xmrigMiner
#endif
#define VALUE_TO_STRING(x) #x
#define VALUE(x) VALUE_TO_STRING(x)

#if !defined(_WIN32) && !defined(WIN32)
namespace
{
volatile sig_atomic_t childSignalTarget = 0;

void forwardSignal(int signal)
{
  const int savedErrno = errno;
  const auto target = childSignalTarget;
  if (target != 0)
  {
    kill(target, signal);
    // A stopped miner must resume before it can handle a shutdown signal.
    kill(target, SIGCONT);
  }
  errno = savedErrno;
}

int runMiner(const std::string& path, int argc, char** argv)
{
  // Console access stops a miner outside the terminal's foreground group.
  const bool separateProcessGroup = !isatty(STDIN_FILENO);
  const pid_t child = fork();
  if (child < 0)
  {
    return errno == 0 ? EXIT_FAILURE : errno;
  }

  if (child == 0)
  {
    if (separateProcessGroup)
    {
      setpgid(0, 0);
    }

    std::vector<char*> args;
    args.reserve(static_cast<size_t>(argc) + 2);
    args.push_back(const_cast<char*>(path.c_str()));
    for (int i = 1; i < argc; ++i)
    {
      args.push_back(argv[i]);
    }
    char daemonized[] = "--daemonized";
    args.push_back(daemonized);
    args.push_back(nullptr);

    execv(path.c_str(), args.data());
    _exit(errno == ENOENT ? 127 : 126);
  }

  if (separateProcessGroup)
  {
    setpgid(child, child);
  }
  childSignalTarget = separateProcessGroup ? -child : child;

  struct sigaction action{};
  action.sa_handler = forwardSignal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGHUP, &action, nullptr);

  int status = 0;
  while (waitpid(child, &status, 0) < 0)
  {
    if (errno != EINTR)
    {
      childSignalTarget = 0;
      return errno == 0 ? EXIT_FAILURE : errno;
    }
  }

  childSignalTarget = 0;
  if (WIFEXITED(status))
  {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status))
  {
    return 128 + WTERMSIG(status);
  }

  return EXIT_FAILURE;
}
}
#endif

bool fileFound(const std::string& filePath)
{
  std::ifstream file(filePath.c_str());
  return file.good();
}

int main(int argc, char** argv)
{
  printf(" * ABOUT        " APP_NAME "Daemon/" APP_VERSION "\n");
  printf(" * BUILD        %s\n", xmrig::buildLinkage());
  printf(" * LIBS         %s\n", xmrig::runtimeLibraryVersions().c_str());
  fflush(stdout);

  std::string ownPath(argv[0]);
#if defined(_WIN32) || defined(WIN32)
  std::string params = " --daemonized";
  for (int i = 1; i < argc; i++)
  {
    params += " ";
    params += argv[i];
  }
#endif

#if defined(_WIN32) || defined(WIN32)
  auto pos = ownPath.rfind('\\');
  std::string minerBinaryName(VALUE(MINER_EXECUTABLE_NAME) ".exe");
#else
  auto pos = ownPath.rfind('/');
  std::string minerBinaryName(VALUE(MINER_EXECUTABLE_NAME));
#endif

  auto fullMinerBinaryPath = ownPath.substr(0, pos + 1) + minerBinaryName;
  while (true)
  {
    auto status = EXIT_SUCCESS;

    // apply update if we have one
    if (fileFound(fullMinerBinaryPath + UPDATE_EXTENSION))
    {
      // remove old backup file
      if (fileFound(fullMinerBinaryPath + BACKUP_EXTENSION))
      {
        status = std::remove((fullMinerBinaryPath + BACKUP_EXTENSION).c_str());
      }

      if (status == EXIT_SUCCESS)
      {
        // rename original to backup
        status = std::rename(fullMinerBinaryPath.c_str(), (fullMinerBinaryPath + BACKUP_EXTENSION).c_str());
        if (status == EXIT_SUCCESS)
        {
          // rename update to original
          status = std::rename((fullMinerBinaryPath + UPDATE_EXTENSION).c_str(), fullMinerBinaryPath.c_str());

#if !defined(_WIN32) && !defined(WIN32)
          if (status == EXIT_SUCCESS)
          {
            // on non-windows system make file executable
            status = chmod(fullMinerBinaryPath.c_str(), S_IRWXU);
          }
#endif
        }

        if (status != EXIT_SUCCESS)
        {
          // try to rollback
          std::rename((fullMinerBinaryPath + BACKUP_EXTENSION).c_str(), fullMinerBinaryPath.c_str());
        }
      }
      else
      {
        // update failed try to remove the update
        std::remove((fullMinerBinaryPath + UPDATE_EXTENSION).c_str());
      }
    }

    // execute miner and wait for result
#if defined(_WIN32) || defined(WIN32)
    status = system(("\"" + fullMinerBinaryPath + "\"" + params).c_str());
    if (status == EXIT_SUCCESS || status == EINVAL || status == SIGINT)
    {
      return status;
    }
#else
    status = runMiner(fullMinerBinaryPath, argc, argv);
    if (status == EXIT_SUCCESS || status == EINVAL || status == 128 + SIGHUP ||
        status == 128 + SIGINT || status == 128 + SIGTERM)
    {
      return status == EXIT_SUCCESS ? EXIT_SUCCESS : status;
    }
#endif

    // Explicit restarts happen immediately; unexpected crashes are throttled
    // to avoid a tight respawn loop.
    if (status != RC_RESTART)
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}
