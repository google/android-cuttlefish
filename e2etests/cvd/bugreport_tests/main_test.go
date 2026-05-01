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
	"archive/zip"
	"io"
	"slices"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestTakeBugreport(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	if err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: "aosp-android-latest-release",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
	}); err != nil {
		t.Fatal(err)
	}
	if err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
		t.Fatal(err)
	}

	if _, err := c.RunCmd(c.TargetBin(), "host_bugreport", "--output=/tmp/host_bugreport.zip"); err != nil {
		t.Fatal(err)
	}

	namesInZip, err := assertZipIntegrity("/tmp/host_bugreport.zip")
	if err != nil {
		t.Errorf("invalid zip file: %s", err)
	}

	expectedNamesSample := []string{
		"cvd-1/cuttlefish_config.json",
		"cvd-1/logs/kernel.log",
		"cvd-1/logs/logcat",
		"cvd-1/logs/launcher.log",
		"fetch.log",
	}
	for _, name := range expectedNamesSample {
		if !slices.Contains(namesInZip, name) {
			t.Errorf("%q not found in zip file", name)
		}
	}
}

// The zip file is verified checking for errors extracting
// each file in the archive.
func assertZipIntegrity(filename string) ([]string, error) {
	names := []string{}
	r, err := zip.OpenReader(filename)
	if err != nil {
		return names, err
	}
	defer r.Close()
	for _, f := range r.File {
		names = append(names, f.Name)
		rc, err := f.Open()
		if err != nil {
			return names, err
		}
		_, readErr := io.Copy(io.Discard, rc)
		rc.Close()
		if readErr != nil {
			return names, readErr
		}
	}
	return names, nil
}
