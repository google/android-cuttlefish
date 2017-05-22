/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "common/metadata/initial_metadata_reader_impl.h"
#include "common/metadata/gce_metadata_attributes.h"

#include <json/json.h>

// TODO(ghartman): Adopt external/gtest.

const char* instance_value = "i_value";
const char* project_value = "p_value";

extern "C" {
// Host builds have trouble because these symbols are not defined.
// TODO(ghartman) Come up with better stubs. #include <atomic> created issues,
// but perhaps there is something more portable than the gcc built-ins.
int32_t android_atomic_inc(volatile int32_t* addr) {
  return __sync_fetch_and_add(addr, 1);
}

int32_t android_atomic_dec(volatile int32_t* addr) {
  return __sync_fetch_and_sub(addr, 1);
}
}

void ExpectEqual(
    const char* test,
    const char* left_desc, const char* left,
    const char* right_desc, const char* right,
    const char* file, int line) {
  if (left == right) {
    return;
  }
  if (left && right && !strcmp(left, right)) {
    return;
  }
  if (!left) {
    left = "(NULL)";
  }
  if (!right) {
    right = "(NULL)";
  }
  fprintf(stderr, "%s: FAIL: strings not equal at %s:%d\n"
          "  %s=\"%s\"\n"
          "  %s=\"%s\"\n", test, file, line, left_desc, left, right_desc,
          right);
  exit(1);
}

#define EXPECT_EQUAL(TEST, LEFT, RIGHT) ExpectEqual(TEST, #LEFT, (LEFT), #RIGHT, (RIGHT), __FILE__, __LINE__)

void ExpectNotEqual(
    const char* test,
    const char* left_desc, const char* left,
    const char* right_desc, const char* right,
    const char* file, int line) {
  if (left != right) {
    return;
  }
  if (left && right && strcmp(left, right)) {
    return;
  }
  if (!left) {
    left = "(NULL)";
  }
  if (!right) {
    right = "(NULL)";
  }
  fprintf(stderr, "%s: FAIL: strings are equal at %s:%d\n"
          "  %s=\"%s\"\n"
          "  %s=\"%s\"\n", test, file, line, left_desc, left, right_desc,
          right);
  exit(1);
}

void ExpectNotEqual(
    const char* test,
    const char* left_desc, int left,
    const char* right_desc, int right,
    const char* file, int line) {
  if (left != right) {
    return;
  }
  fprintf(stderr, "%s: FAIL: ints are equal at %s:%d\n"
          "  %s=\"%d\"\n"
          "  %s=\"%d\"\n", test, file, line, left_desc, left, right_desc,
          right);
  exit(1);
}

#define EXPECT_NOT_EQUAL(TEST, LEFT, RIGHT) ExpectNotEqual(TEST, #LEFT, (LEFT), #RIGHT, (RIGHT), __FILE__, __LINE__)

void ExpectNotNull(
    const char* test, const char* left_desc, void *left,
    const char* file, int line) {
  if (left) {
    return;
  }
  fprintf(stderr, "%s: FAIL: null for %s at %s:%d\n",
          test, left_desc, file, line);
  exit(1);
}

#define EXPECT_NOT_NULL(TEST, LEFT) ExpectNotNull(TEST, #LEFT, (LEFT), __FILE__, __LINE__)

class TestMetadataReader : public avd::InitialMetadataReaderImpl {
 public:
  TestMetadataReader(const char* path) : InitialMetadataReaderImpl() {
    Init(path);
  }
};

struct TestLine {
  const char* path;
  const char* key;
  const char* value;
};

struct TestCase {
  const char* expected_value;
  const char* key;
  const TestLine * lines;
};

TestLine EmptyFileLines[] = {
  {NULL, NULL, NULL},
};

const char* some_key = "some_key";

TestCase EmptyFile = {
  NULL,
  some_key,
  EmptyFileLines
};

TestLine InstanceFileLines[] =   {
  {GceMetadataAttributes::kInstancePath, some_key, instance_value},
  {NULL, NULL, NULL},
};

TestCase InstanceFile = {
  instance_value,
  some_key,
  InstanceFileLines
};

TestLine ProjectFileLines[] =   {
  {GceMetadataAttributes::kProjectPath, some_key, project_value},
  {NULL, NULL, NULL},
};

TestCase ProjectFile = {
  project_value,
  some_key,
  ProjectFileLines
};

TestLine InstanceBeforeProjectLines[] =   {
  {GceMetadataAttributes::kInstancePath, some_key, instance_value},
  {GceMetadataAttributes::kProjectPath, some_key, project_value},
  {NULL, NULL, NULL},
};

TestCase InstanceBeforeProject = {
  instance_value,
  some_key,
  InstanceBeforeProjectLines
};

TestLine ProjectBeforeInstanceLines[] =   {
  {GceMetadataAttributes::kProjectPath, some_key, project_value},
  {GceMetadataAttributes::kInstancePath, some_key, instance_value},
  {NULL, NULL, NULL},
};

TestCase ProjectBeforeInstance = {
  instance_value,
  some_key,
  ProjectBeforeInstanceLines
};

TestLine ProjectSetInstanceSetEmptyLines[] =   {
  {GceMetadataAttributes::kProjectPath, some_key, project_value},
  {GceMetadataAttributes::kInstancePath, some_key, ""},
  {NULL, NULL, NULL},
};

TestCase ProjectSetInstanceSetEmpty = {
  "",
  some_key,
  ProjectSetInstanceSetEmptyLines
};

void WriteLines(const char* name, const TestLine* data, int fd) {
  Json::Value root(Json::objectValue);
  while (data->path && data->key && data->value) {
    if (data->path == GceMetadataAttributes::kProjectPath) {
      root["project"]["attributes"][data->key] = data->value;
    } else if (data->path == GceMetadataAttributes::kInstancePath) {
      root["instance"]["attributes"][data->key] = data->value;
    } else {
      root[data->path][data->key] = data->value;
    }
    ++data;
  }
  int my_fd = dup(fd);
  EXPECT_NOT_EQUAL(name, my_fd, -1);
  FILE * dest = fdopen(my_fd, "w");
  EXPECT_NOT_NULL(name, dest);
  fputs("Metadata-Flavor: Google\r\n\r\n", dest);
  fputs(Json::FastWriter().write(root).c_str(), dest);
  EXPECT_NOT_EQUAL(name, fclose(dest), EOF);
}

void RunTest(const char* name, const TestCase& test) {
  char *filename = strdup("/tmp/testXXXXXX");
  EXPECT_NOT_NULL(name, filename);
  int fd = mkstemp(filename);
  EXPECT_NOT_EQUAL(name, fd, -1);
  WriteLines(name, test.lines, fd);
  TestMetadataReader* reader = new TestMetadataReader(filename);
  EXPECT_NOT_NULL(name, reader);
  EXPECT_EQUAL(name, reader->GetValueForKey(test.key), test.expected_value);
  delete reader;
  EXPECT_NOT_EQUAL(name, close(fd), -1);
  EXPECT_NOT_EQUAL(name, unlink(filename), -1);
  free(filename);
  printf("%s: PASS\n", name);
  fflush(stdout);
}

#define RUN_TEST(CONFIG) RunTest(#CONFIG, (CONFIG))

TestLine SpuriousPathLines[] =   {
  {"spurious_path", some_key, instance_value},
  {NULL, NULL, NULL},
};

TestCase SpuriousPath = {
  NULL,
  some_key,
  SpuriousPathLines
};

TestLine SpuriousKeyLines[] =   {
  {GceMetadataAttributes::kInstancePath, "spurious", instance_value},
  {NULL, NULL, NULL},
};

TestCase SpuriousKey = {
  NULL,
  some_key,
  SpuriousKeyLines
};

int main(int /*argc*/, char* /*argv*/[]) {
  RUN_TEST(EmptyFile);
  RUN_TEST(InstanceFile);
  RUN_TEST(ProjectFile);
  RUN_TEST(InstanceBeforeProject);
  RUN_TEST(ProjectBeforeInstance);
  RUN_TEST(ProjectSetInstanceSetEmpty);
  RUN_TEST(SpuriousPath);
  RUN_TEST(SpuriousKey);
  return 0;
}
