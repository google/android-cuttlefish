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
	"testing"

	e2etests "github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestBootloaderQemu(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	if _, err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch:    "git_main",
		DefaultBuildTarget:    "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		BootloaderBuildBranch: "aosp_u-boot-mainline",
		BootloaderBuildTarget: "u-boot_qemu_x86_64",
	}); err != nil {
		t.Fatal(err)
	}

	if !e2etests.FileExists(filepath.Join(c.TempDir(), "bootloader")) {
		t.Fatalf("expected fetched bootloader at %s", filepath.Join(c.TempDir(), "bootloader"))
	}

	if _, err := c.CVDCreate(e2etests.CreateArgs{
		Args: []string{
			"--vm_manager=qemu_cli",
			"--gpu_mode=guest_swiftshader",
		},
	}); err != nil {
		t.Fatal(err)
	}

	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatal(err)
	}
}
