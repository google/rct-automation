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

#ifndef REGISTERABLE_H
#define REGISTERABLE_H

#include <map>
#include <string>

#define REGISTER_COMMAND(x) static bool x##init = (new x())->register_commands()
#define REGISTER_REGISTRAR(x, callbacktype) \
 \
class Registrar { \
 public: \
  Registrar () {}; \
  virtual ~Registrar () {}; \
  typedef std::map<std::string, callbacktype > CallbackMap; \
  typedef callbacktype callback; \
 \
  static void add_callback(std::string name, callbacktype func) { \
    get_callbackmap().insert(std::pair<std::string, callbacktype >(name, func)); \
  } \
  static CallbackMap &get_callbackmap() { \
    static CallbackMap map; \
    return map; \
  } \
  bool register_commands() { \
    add_callback(get_command(),get_callback()); \
    return true; \
  } \
 private: \
  virtual const std::string get_command() = 0; \
  virtual callbacktype get_callback() = 0; \
}

#endif
