/*
 * Copyright (C) 2017 The Android Open Source Project
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

/* Utility that uses an adb connection as the login shell. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  char** new_argv = malloc((argc + 5) * sizeof(char*));
  new_argv[0] = "/usr/bin/adb";
  new_argv[1] = "-s";
  new_argv[2] = "CUTTLEFISHAVD01";
  new_argv[3] = "shell";
  new_argv[4] = "/system/bin/sh";

  // Some important data is lost before this point, and there are
  // no great recovery options:
  // * ssh with no arguments comes in with 1 arg of -adbshell. The command
  //   given above does the right thing if we don't invoke the shell.
  if (argc == 1) {
    new_argv[4] = 0;
  }
  // * simple shell commands come in with a -c and a single string. The
  //   problem here is that adb doesn't preserve spaces, so we need
  //   to do additional escaping. The best compromise seems to be to
  //   throw double quotes around each string.
  for (int i = 1; i < argc; ++i) {
    size_t buf_size = strlen(argv[i]) + 4;
    new_argv[i + 4] = malloc(buf_size);
    snprintf(new_argv[i + 4], buf_size, "\"%s\"", argv[i]);
  }
  //
  // * scp seems to be pathologically broken when paths contain spaces.
  //   spaces aren't properly escaped by gcloud, so scp will fail with
  //   "scp: with ambiguous target." We might be able to fix this with
  //   some creative parsing of the arguments, but that seems like
  //   overkill.
  new_argv[argc + 4] = 0;
  execv(new_argv[0], new_argv);
  // This never should happen
  return 2;
}
