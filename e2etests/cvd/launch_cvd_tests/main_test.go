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
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

// Injected at link time via `x_defs` in BUILD.bazel.
var (
	branch string
	target string
)

func TestLaunchCvd(t *testing.T) {
	if branch == "" || target == "" {
		t.Fatalf("branch and target must be set via x_defs in BUILD.bazel (branch=%q, target=%q)", branch, target)
	}

	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	if _, err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: branch,
		DefaultBuildTarget: target,
	}); err != nil {
		t.Fatal(err)
	}

	if err := c.LaunchCVD(e2etests.CreateArgs{}); err != nil {
		t.Fatal(err)
	}
}
