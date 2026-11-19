// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"os"
	"testing"

	"github.com/bazelbuild/rules_go/go/runfiles"
	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestCvdLoad(t *testing.T) {
	// Set per target via `env` in BUILD.bazel.
	rlocation := os.Getenv("CVD_LOAD_CONFIG")
	if rlocation == "" {
		t.Fatal("CVD_LOAD_CONFIG environment variable is not set")
	}
	configpath, err := runfiles.Rlocation(rlocation)
	if err != nil {
		t.Fatalf("failed to find load config runfile from %s: %v", rlocation, err)
	}
	loadconfig, err := os.ReadFile(configpath)
	if err != nil {
		t.Fatalf("failed to read load config from %s: %v", configpath, err)
	}

	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	err = c.CVDCreateWithConfigFile(e2etests.LoadArgs{
		LoadConfig: string(loadconfig),
		Env:        map[string]string{"HOME": t.TempDir()},
	})
	if err != nil {
		t.Fatal(err)
	}
}
