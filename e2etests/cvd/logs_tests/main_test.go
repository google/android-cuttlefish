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
	"slices"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestPrintLogs(t *testing.T) {
	testcases := []struct {
		branch string
		target string
	}{
		{
			branch: "git_main",
			target: "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		},
	}
	c := e2etests.TestContext{}
	for _, tc := range testcases {
		t.Run("foo", func(t *testing.T) {
			c.SetUp(t)
			defer c.TearDown()

			if err := c.CVDFetch(e2etests.FetchArgs{
				DefaultBuildBranch: tc.branch,
				DefaultBuildTarget: tc.target,
			}); err != nil {
				t.Fatal(err)
			}

			if err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
				t.Fatal(err)
			}

			res, err := c.RunCmd(c.TargetBin(), "logs")
			if err != nil {
				t.Fatal(err)
			}
			stdOut := res.Stdout

			listedNames := []string{}
			lines := strings.Split(stdOut, "\n")
			for _, line := range lines[:len(lines)-1] {
				fields := strings.Fields(line)
				if len(fields) != 2 {
					t.Errorf("line %q does not match format '<NAME> <PATH>'", line)
				}
				res, err := c.RunCmd("stat", fields[1])
				if err != nil {
					t.Fatal(res.Stderr)
				}
				listedNames = append(listedNames, fields[0])
			}
			keyExpectedNames := []string{"logcat", "launcher.log", "kernel.log"}
			for _, name := range keyExpectedNames {
				if !slices.Contains(listedNames, name) {
					t.Fatalf("missing log name: %q", name)
				}
			}

			res, err = c.RunCmd(c.TargetBin(), "logs", "--print", "launcher.log")
			if err != nil {
				t.Fatal(err)
			}
			stdOut = res.Stdout
			if len(stdOut) == 0 {
				t.Fatalf("empty launcher.log")
			}

		})
	}
}
