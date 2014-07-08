/*
 *   Copyright 2012-2014 Google, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <deque>
#include <cstdlib>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include "dirent.h"
#include <stdlib.h>
#include "playableitem.h"
#include "mplayersession.h"
#include "stdio.h"
#include <string>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <sys/stat.h>
#include <glog/logging.h>
#include "fcntl.h"
#include "playerstate.pb.h"
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <boost/lexical_cast.hpp>
#include <gflags/gflags.h>

DEFINE_string(mplayer, "mplayer", "Mplayer binary to use");
DEFINE_int32(mplayertimeout, 6, "Number of seconds to wait before killing a dead mplayer process.");
DEFINE_string(mplayer_errorlog, "/dev/null", "Location to send mplayer stderr.");

MplayerSession::MplayerSession() :
  slave_pipe_(0),
  mplayer_stdout_(0),
  mplayer_fd_(0),
  errorfd_(open(FLAGS_mplayer_errorlog.c_str(), O_WRONLY)),
  mplayer_pid_(-1),
  last_alive_(0) {
  CHECK(errorfd_ != -1) << "Unable to open file " +  FLAGS_mplayer_errorlog + "for mplayer errorlog: " << errno; 
}
bool MplayerSession::Play(PlayableItem& item) {
  if  (item.data().has_playableitemid()) {
    item.IncrementPlaycount();
    item.Update();
  }
  return Play(item.data());
}
bool MplayerSession::Play(const automation::PlayableItem& item) {
  boost::mutex::scoped_lock state_lock(state_mutex_);
  state_.mutable_now_playing()->MergeFrom(item);
  state_lock.unlock();

  LOG(INFO) << "requesting playing of " << item.filename();
  boost::mutex::scoped_lock Lock(mutex_);

  int slave_pipefd[2];
  int pipefd[2];

  CHECK(pipe2(slave_pipefd, 0) == 0);
  CHECK(pipe2(pipefd, 0) == 0);

  char pipearg[128];

  char endpos[16];
  char cache[16];
  snprintf(pipearg, sizeof(pipearg), "file=/dev/fd/%d",slave_pipefd[0]);

  std::vector<const char*> argv;
  argv.push_back("mplayer");
  argv.push_back("-quiet");
  argv.push_back("-msglevel"), argv.push_back("all=0:global=4");
  argv.push_back("-slave");
  if (item.type() == automation::PlayableItem::WEBSTREAM) {
    snprintf(endpos, sizeof endpos, "%ld", item.duration());
    snprintf(cache, sizeof cache, "%d", item.cache());

    argv.push_back( "-endpos");
    argv.push_back(endpos);

    argv.push_back("-cache");
    argv.push_back(cache);
  }
  argv.push_back("-input");
  argv.push_back(pipearg);
  argv.push_back(item.filename().c_str());
  argv.push_back(NULL);
  std::stringstream cmdline;
  for (unsigned int i = 0; i < argv.size() ; ++i) {
    cmdline << argv[i] << " ";
  }
  VLOG(2) << cmdline.str();

  mplayer_pid_ = fork();
  CHECK(mplayer_pid_ != -1);

  if (!mplayer_pid_) {
    close(pipefd[0]);
    close(slave_pipefd[1]);
    CHECK(dup2(pipefd[1], 1) != -1);
    CHECK(dup2(errorfd_, 2) != -1);
    for (int i = 3; i <= AUTOMATION_MAX_FD && i != slave_pipefd[0]; ++i) {
      close(i);
    }
#ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    setpgid(0, 0);
#endif
    execvp(FLAGS_mplayer.c_str(), const_cast<char**>(&argv[0]));
  } else {
    close(pipefd[1]);
    close(slave_pipefd[0]);
    mplayer_stdout_ = fdopen(pipefd[0], "r");
    fcntl(pipefd[0], F_SETFL, O_RDONLY | O_NONBLOCK);
    setvbuf(mplayer_stdout_, (char *) NULL, _IOLBF, 0);

    slave_pipe_ = fdopen(slave_pipefd[1], "a");
    CHECK(slave_pipe_ != NULL) << "Unable to create stream from slave fd with errno: " << errno;

    mplayer_fd_ = pipefd[0];
    fflush(mplayer_stdout_);
  }

  VLOG(2) << "Waiting for pid " << mplayer_pid_;
  while (waitpid(mplayer_pid_, NULL, WNOHANG) == 0) {
    Lock.unlock();
    usleep(250*1000); // 250ms for any other client to grab the mplayer session

    GetProperty("pause");
    GetProperty("time_pos");
    GetProperty("length");
    GetProperty("metadata");

    Lock.lock();
    if (feof(mplayer_stdout_) || feof(slave_pipe_) || is_timedout()) {
      goto death;
    }
  }
 death:
  VLOG(5) << "In mplayer death";
  kill(mplayer_pid_, 9);
  waitpid(mplayer_pid_, NULL, 0);

  state_lock.lock();
  state_.Clear();

  CHECK(close(pipefd[0]) != -1);
  CHECK(close(slave_pipefd[1]) != -1);

  mplayer_pid_ = -1;
  if (mplayer_stdout_) {
    fclose(mplayer_stdout_);
    mplayer_stdout_ = NULL;
  }

  if (slave_pipe_ != NULL) {
    fclose(slave_pipe_);
    slave_pipe_ = NULL;
  }

  return true;
}

void MplayerSession::Pause() {
  boost::mutex::scoped_lock player_lock(mutex_);
  boost::mutex::scoped_lock state_lock(state_mutex_);
  VLOG(5) << "in player pause";
  if (slave_pipe_ == NULL) {
    return;
  }
  state_.set_paused(!state_.paused());

  state_lock.unlock();
  VLOG(5) << "about to pause";
  CHECK(fprintf(slave_pipe_, "pause\n"));
  fflush(slave_pipe_);
}
void MplayerSession::Unpause() {
  boost::mutex::scoped_lock player_lock(mutex_);
  boost::mutex::scoped_lock state_lock(state_mutex_);

  if (slave_pipe_ == NULL) {
    return;
  }
  if (state_.paused()) {
    player_lock.unlock();
    state_lock.unlock();
    // A bit of a race here; it's possible that our state could change
    // after we unlock.  To be slightly safer here, we should implement
    // a version of pause to be used when the proper locks are held.
    // Possibly not worth worrying about, however.
    pause();
  }
  return; 
}

void MplayerSession::Stop() {
  if (slave_pipe_ == NULL || mplayer_pid_ == -1) {
    return;
  }
  kill(mplayer_pid_, 9);
}

void MplayerSession::MergeState(automation::PlayerState* dest) {
  boost::mutex::scoped_lock state_lock(state_mutex_);
  VLOG(5) << "Merging out our state: " << state_.DebugString();
  dest->MergeFrom(state_);
}

void MplayerSession::SetSpeed(double speed) {
  boost::mutex::scoped_lock Lock(mutex_);
  if (slave_pipe_ == NULL || mplayer_pid_ == -1) {
    return;
  }
  fprintf(slave_pipe_, "pausing_keep_force set_property speed %f\n", speed);
}
void MplayerSession::Seek(double timepos) {
  boost::mutex::scoped_lock Lock(mutex_);
  if (slave_pipe_ == NULL || mplayer_pid_ == -1) {
    return;
  }
  fprintf(slave_pipe_, "pausing_keep_force set_property time_pos %f\n", timepos);
}
  
std::string MplayerSession::GetProperty(std::string property_name) {
  char buf[2048];

  if (mplayer_stdout_ == NULL || slave_pipe_ == NULL || feof(mplayer_stdout_) || feof(slave_pipe_)) {
    return "EOF";
  }

  boost::mutex::scoped_lock player_lock(mutex_);

  fflush(mplayer_stdout_);
  char expected_answer[512];
  snprintf(expected_answer, sizeof(expected_answer), "ANS_%s=", property_name.c_str());

  VLOG(80) << "pausing_keep_force get_property " << property_name.c_str();
  if (fprintf(slave_pipe_, "pausing_keep_force get_property %s\n", property_name.c_str()) < 0) {
    LOG(WARNING) << "Unable to send property " << property_name;
    return "EOF";
  }

  fflush(slave_pipe_);

  fd_set readfd;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 250000;
  FD_ZERO(&readfd);
  FD_SET(fileno(mplayer_stdout_), &readfd);
  while (select(fileno(mplayer_stdout_)+1, &readfd, NULL, NULL, &tv) == 1) {
    if (fgets(buf, sizeof(buf), mplayer_stdout_) == NULL) {
      // This is expected if we are between tracks or have just started.
      VLOG(15) << "Unable to read property " << property_name << ": " << ferror(mplayer_stdout_);
      return "EOF";
    }
    buf[sizeof(buf)-1] = '\0';
    VLOG(80) << buf;
    if (strncmp(buf, expected_answer, strlen(expected_answer))) {
      VLOG(15) << "Unexpected answer " << buf << "when expecting " << expected_answer;
      continue;
    } else {
      if (buf[strlen(buf)-1] == '\n') {
        buf[strlen(buf)-1] = '\0';
      } else {
        LOG(WARNING) << "No newline from mplayer?";
      }
      last_alive_ = time(NULL);
      std::string result(buf+strlen(property_name.c_str())+strlen("ANS_="));
      boost::mutex::scoped_lock state_lock(state_mutex_);
      if (property_name == "pause") {
        state_.set_paused(result.find("yes") != std::string::npos);
      } else if (property_name == "time_pos") {
        state_.set_time_pos(atof(result.c_str()));
      } else if (property_name == "length") {
        state_.set_length(atof(result.c_str()));
      } else if (property_name == "metadata") {
        state_.set_metadata(result);
      } else if (property_name == "path") {
        state_.set_path(result);
      }
      return result;
    }
  }
  VLOG(80) << "timeout";
  return "EOF";
}
bool MplayerSession::is_timedout() {
  if ((time(NULL) - last_alive_) > FLAGS_mplayertimeout) {
    LOG(WARNING) << "Mplayer timed out";
    return true;
  }
  return false;
}
 
