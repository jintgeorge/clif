/*
 * Copyright 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef CLIF_TESTING_DEFAULT_ARGS_H_
#define CLIF_TESTING_DEFAULT_ARGS_H_

#include <memory>
#include <vector>

class MyClass {
 public:
  enum Enum {
    E1 = 321,
    E2 = 432,
    E3 = 543
  };

  enum Flag {
    F1 = 1,
    F2 = 2,
    F3 = 4,
    F5 = 8
  };

  struct Arg {
    int e;
  };

  int MethodWithDefaultClassArg(Arg arg = {10}, int i = 100) {
    return arg.e + i;
  }

  int MethodWithDefaultEnumArg(Enum e = kMyEnum, int i = 100) {
    return e + i;
  }

  int MethodWithDefaultPtrArg(Arg* arg = nullptr, int i = 100) {
    if (arg == nullptr) {
      return i;
    } else {
      return arg->e + i;
    }
  }

  int MethodWithDefaultFlag(int flag = F1 | F2, int i = 16) {
    return flag | i;
  }

 private:
  static const Enum kMyEnum;
};

const MyClass::Enum MyClass::kMyEnum = E2;

#endif  // CLIF_TESTING_DEFAULT_ARGS_H_
