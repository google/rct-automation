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
#include <boost/tokenizer.hpp>
#include "playableitem.h"
#include "playlist.h"
#include "automationstate.h"
#include "mplayersession.h"
#include <glog/logging.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "sqlite3.h"
#include <boost/tr1/regex.hpp>
#include <boost/weak_ptr.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef USE_RE2
#include <re2/re2.h>
#else
#include <sys/types.h>
#include <regex.h>
#endif
#include "playlist.pb.h"
#include "playableitem.pb.h"
#include "protostore.h"

#define MAX(a,b) ((a>b)?a:b)

bool PlayableItem::fetch(const std::string& filename) {
  boost::mutex::scoped_lock lock(mutex_);
  canonical_.Clear();
  canonical_.set_filename(filename);
  bool result = Load(&canonical_);

  if (canonical_.has_filename() && !canonical_.has_duration()) {
    canonical_.set_duration(CalculateDuration());
  }
  
  return result;
}

PlayableItem::PlayableItem(sqlite3 *db) :
  automation::ThreadSafeProto<automation::PlayableItem>(db) {
}

void PlayableItem::IncrementPlaycount() {
  // There is a slight race here, because if we are being incremented in two
  // threads then it's possible we were both created with original state,
  // and we'll only count this once.  But triggering this would require, say,
  // playback through the API at the same time as through the main thread,
  // which would maybe result in a neat double-tracking effect but probably only
  // really be one logical playback as far as anyone is concerned, so this doesn't
  // really bother me.  If it bothers you, however, we could just do a SQL UPDATE
  // here instead.
  boost::mutex::scoped_lock lock(mutex_);
  canonical_.set_playcount(canonical_.playcount() + 1);
}

#ifdef USE_RE2
bool PlayableItem::matches(const RE2& re) {
  CHECK(re.ok());
  boost::mutex::scoped_lock lock(mutex_);
  return RE2::PartialMatch(canonical_.description(), re) || RE2::PartialMatch(canonical_.filename(), re);
}
#else
bool PlayableItem::matches(const regex_t& re) {
  boost::mutex::scoped_lock lock(mutex_);
  return !regexec(&re, canonical_.description().c_str(), 0, NULL, 0) ||
         !regexec(&re, canonical_.filename().c_str(), 0, NULL, 0);
}
#endif

int PlayableItem::CalculateDuration() {
  pid_t child;
  int pipefd[2];
  std::string filename;
  
  if (!canonical_.has_filename()) {
    LOG(INFO) << "Asked about duration but provided no filename";
    return -1;
  }
  filename = canonical_.filename();

  struct stat statobj;
  if (stat(filename.c_str(), &statobj) || !statobj.st_size || !S_ISREG(statobj.st_mode)) {
    LOG(INFO) << "Asked about duration of an invalid file " << filename;
    return -1;
  }
 
  CHECK(pipe2(pipefd, 0) == 0);
  char buf[1000];
  int duration;
  int errorfd = open("/dev/null", O_WRONLY);
  CHECK(errorfd != -1);
  LOG(INFO) << "In CalculateDuration for " << filename;
  duration = -1;

  child = fork();
  CHECK(child != -1);
  if (!child) {
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(errorfd, STDERR_FILENO);
    for (int i = 3; i <= AUTOMATION_MAX_FD; ++i) {
      close(i);
    }
    close(STDIN_FILENO);
    execlp("mplayer", "", "-noconsolecontrols", "-ao", "pcm:file=/dev/null", filename.c_str(), NULL);

  } else {
    FILE *mplayer_stdout;
    mplayer_stdout = fdopen(pipefd[0], "r");
    close(pipefd[1]);
    int status;

    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    std::tr1::cmatch res;
    std::tr1::regex rx("A:[ ]*([0-9.]*)");

    while (fgets(buf, sizeof(buf), mplayer_stdout) != NULL) {
      std::string len(buf);
      tokenizer tokens(len, boost::char_separator<char>("\r"));
      for (tokenizer::const_iterator it = tokens.begin(); it != tokens.end(); ++it) {
        if (std::tr1::regex_search(it->c_str(), res, rx)) {
          duration = MAX(duration,ceil(strtod(res[1].str().c_str(), NULL)));
        }
      }
    }
    waitpid(child, &status, 0);
    close(pipefd[0]);
    close(errorfd);
  }
  return duration;
}
