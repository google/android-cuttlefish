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
	"encoding/json"
	"path"
	"path/filepath"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

type fleetGroup struct {
	MetricsDir string `json:"metrics_dir"`
}

type fleetOutput struct {
	Groups []fleetGroup `json:"groups"`
}

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
	defer c.TearDown()

	fetchArgs := e2etests.FetchArgs{
		DefaultBuildBranch: "aosp-android-latest-release",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
	}
	if _, err := c.CVDFetch(fetchArgs); err != nil {
		t.Fatalf("failed to run `%s fetch`: %v", c.TargetBin(), err)
	}

	// The test's tempdir is the `--target_directory` of fetch.
	fetchMetricsDir := path.Join(c.TempDir(), "metrics")
	fetchPatterns := []string{
		"fetch_start*.txtpb",
		"fetch_complete*.txtpb",
	}
	for _, p := range fetchPatterns {
		if !anyFileExists(path.Join(fetchMetricsDir, p)) {
			t.Fatalf("failed to find a file matching %q in %q", p, fetchMetricsDir)
		}
	}

	if _, err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
		t.Fatalf("failed to run `%s create`: %v", c.TargetBin(), err)
	}

	res, err := c.RunCmd(c.TargetBin(), "fleet")
	if err != nil {
		t.Fatalf("failed to run `%s fleet`: %v", c.TargetBin(), err)
	}
	var fleet fleetOutput
	if err := json.Unmarshal([]byte(res.Stdout), &fleet); err != nil {
		t.Fatalf("failed to parse `%s fleet` output: %v", c.TargetBin(), err)
	}
	if len(fleet.Groups) == 0 || fleet.Groups[0].MetricsDir == "" {
		t.Fatalf("failed to find metrics directory in `%s fleet` output: %s", c.TargetBin(), res.Stdout)
	}
	metricsdir := fleet.Groups[0].MetricsDir
	if !e2etests.DirectoryExists(metricsdir) {
		t.Fatalf("failed to find directory %q", metricsdir)
	}

	devicePatterns := []string{
		"device_instantiation*.txtpb",
		"device_boot_start*.txtpb",
		"device_boot_complete*.txtpb",
	}
	for _, p := range devicePatterns {
		if !anyFileExists(path.Join(metricsdir, p)) {
			t.Fatalf("failed to find a file matching %q in %q", p, metricsdir)
		}
	}

	if err := c.CVDStop(); err != nil {
		t.Fatalf("failed to run `%s stop`: %v", c.TargetBin(), err)
	}

	if !anyFileExists(path.Join(metricsdir, "device_stop*.txtpb")) {
		t.Errorf("failed to find `device_stop*.txtpb` file.")
	}
}
