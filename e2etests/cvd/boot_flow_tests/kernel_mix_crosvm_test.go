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
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestKernelMixCrosvm(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	if _, err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: "git_main",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		KernelBuildBranch:  "aosp_kernel-common-android17-6.18",
		KernelBuildTarget:  "kernel_virt_x86_64",
	}); err != nil {
		t.Fatal(err)
	}

	if !e2etests.FileExists(filepath.Join(c.TempDir(), "kernel")) {
		t.Fatalf("expected fetched kernel at %s", filepath.Join(c.TempDir(), "kernel"))
	}
	if !e2etests.FileExists(filepath.Join(c.TempDir(), "initramfs.img")) {
		t.Fatalf("expected fetched initramfs.img at %s", filepath.Join(c.TempDir(), "initramfs.img"))
	}

	kernelBuildID, err := c.FetcherConfigBuildID("kernel_build")
	if err != nil {
		t.Fatal(err)
	}

	if _, err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
		t.Fatal(err)
	}

	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatal(err)
	}

	res, err := c.RunCmd("adb", "shell", "uname", "-r")
	if err != nil {
		t.Fatalf("failed to query guest kernel version: %v", err)
	}
	if !strings.Contains(res.Stdout, "ab"+kernelBuildID) {
		t.Fatalf("expected guest kernel uname -r to contain mixed kernel build id ab%s, got %q", kernelBuildID, strings.TrimSpace(res.Stdout))
	}
}
