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
	"bufio"
	"fmt"
	"regexp"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

var allowedCvdFetchLinePatterns = []string{
	`Starting fetch to.*`,
	`Downloading host package.*`,
	`Completed target fetch to.*`,
	`Completed all fetches`,
}
var allowedCvdFetchRegexp = regexp.MustCompile(strings.Join(allowedCvdFetchLinePatterns, "|"))

var allowedCvdCreateLinePatterns = []string{
	`Point your browser to.*`,
	`Serial console is disabled;.*`,
	`Launcher Build ID`,
	`Virtual device booted successfully`,
	`group.*`,
}
var allowedCvdCreateRegexp = regexp.MustCompile(strings.Join(allowedCvdCreateLinePatterns, "|"))

func checkForLogspam(t *testing.T, res e2etests.CommandOutput, allowed *regexp.Regexp) {
	scanner := bufio.NewScanner(strings.NewReader(res.Stdout))
	for scanner.Scan() {
		line := scanner.Text()
		if !allowed.MatchString(line) {
			t.Errorf("unexpected log: %s", line)
		}
	}
}

func TestCvdCreate(t *testing.T) {
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
		t.Run(fmt.Sprintf("BUILD=%s/%s", tc.branch, tc.target), func(t *testing.T) {
			c.SetUp(t)
			defer c.TearDown()

			res, err := c.CVDFetch(e2etests.FetchArgs{
				DefaultBuildBranch: tc.branch,
				DefaultBuildTarget: tc.target,
			})
			if err != nil {
				t.Fatalf("cvd fetch failed with %v, stderr:%s", err, res.Stderr)
			}

			checkForLogspam(t, res, allowedCvdFetchRegexp)

			res, err = c.CVDCreate(e2etests.CreateArgs{})
			if err != nil {
				t.Fatalf("cvd create failed with %v, stderr:%s", err, res.Stderr)
			}

			checkForLogspam(t, res, allowedCvdCreateRegexp)
		})
	}
}
