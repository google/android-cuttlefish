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
	"fmt"
	"path"
	"path/filepath"
	"regexp"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func anyFileExists(pattern string) bool {
	matches, err := filepath.Glob(pattern)
	if err != nil {
		return false
	}
	return len(matches) > 0
}

func TestMetrics(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)

	if err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: "aosp-android-latest-release",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
	}); err != nil {
		t.Fatal(err)
	}

	if err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
		t.Fatal(err)
	}

	var metricsdir string
	err := func() error {
		output, err := c.RunCmd("cvd", "fleet")
		if err != nil {
			return fmt.Errorf("failed to run `cvd fleet`")
		}

		re := regexp.MustCompile(`"metrics_dir" : "(.*)",`)
		matches := re.FindStringSubmatch(output)
		if len(matches) != 2 {
			return fmt.Errorf("failed to find metrics directory.")
		}

		metricsdir = matches[1]
		if !e2etests.DirectoryExists(metricsdir) {
			return fmt.Errorf("failed to find directory %s", metricsdir)
		}

		patterns := []string{
			"device_instantiation*.txtpb",
			"device_boot_start*.txtpb",
			"device_boot_complete*.txtpb",
		}
		for _, p := range patterns {
			if !anyFileExists(path.Join(metricsdir, p)) {
				return fmt.Errorf("failed to find a file matching `%s`", p)
			}
		}
		return nil
	}()
	if err != nil {
		t.Fatal(err)
	}

	if err := c.CVDStop(); err != nil {
		t.Fatal(err)
	}

	if !anyFileExists(path.Join(metricsdir, "device_stop*.txtpb")) {
		t.Errorf("failed to find `device_stop*.txtpb` file.")
	}

	c.TearDown()
}
