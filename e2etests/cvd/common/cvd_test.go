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

package e2etests

import (
	"slices"
	"testing"
)

func TestParseCVDStatusJSON(t *testing.T) {
	tests := []struct {
		name      string
		input     string
		wantCount int
		wantName  string
		wantStat  string
		wantErr   bool
	}{
		{
			name: "Array format",
			input: `[
				{
					"adb_port": 6520,
					"adb_serial": "127.0.0.1:6520",
					"instance_name": "ins-1",
					"status": "Running"
				}
			]`,
			wantCount: 1,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name: "Group object format",
			input: `{
				"instances": [
					{
						"adb_port": 6520,
						"adb_serial": "127.0.0.1:6520",
						"instance_name": "ins-1",
						"status": "Running"
					},
					{
						"adb_port": 6521,
						"adb_serial": "127.0.0.1:6521",
						"instance_name": "ins-2",
						"status": "Running"
					}
				]
			}`,
			wantCount: 2,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name: "Log prefix with group object",
			input: `[INFO 2026-09-10 12:34:56] cvd status output:
			{
				"instances": [
					{
						"adb_port": 6520,
						"adb_serial": "127.0.0.1:6520",
						"instance_name": "ins-1",
						"status": "Running"
					}
				]
			}`,
			wantCount: 1,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name: "Log prefix with array",
			input: `[INFO 2026-09-10 12:34:56] cvd status output:
			[
				{
					"adb_port": 6520,
					"adb_serial": "127.0.0.1:6520",
					"instance_name": "ins-1",
					"status": "Running"
				}
			]`,
			wantCount: 1,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name: "Single object format",
			input: `{
				"adb_port": 6520,
				"adb_serial": "127.0.0.1:6520",
				"instance_name": "ins-1",
				"status": "Running"
			}`,
			wantCount: 1,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name:    "Invalid output",
			input:   "error: cvd server is not running",
			wantErr: true,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			entries, err := ParseCVDStatusJSON(tc.input)
			if tc.wantErr {
				if err == nil {
					t.Fatalf("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if len(entries) != tc.wantCount {
				t.Fatalf("expected %d entries, got %d", tc.wantCount, len(entries))
			}
			if entries[0].InstanceName != tc.wantName {
				t.Errorf("expected instance name %q, got %q", tc.wantName, entries[0].InstanceName)
			}
			if entries[0].Status != tc.wantStat {
				t.Errorf("expected status %q, got %q", tc.wantStat, entries[0].Status)
			}
		})
	}
}

func TestBuildFetchCmd(t *testing.T) {
	got := buildFetchCmd("cvd", "/tmp/dir", "gce", FetchArgs{
		DefaultBuildBranch:            "git_main",
		DefaultBuildTarget:            "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		BootloaderBuildBranch:         "aosp_u-boot-mainline",
		BootloaderBuildTarget:         "u-boot_crosvm_x86_64",
		AndroidEfiLoaderBuildBranch:   "aosp_uefi-gbl-mainline",
		AndroidEfiLoaderBuildTarget:   "gbl_efi_dist_and_test",
		AndroidEfiLoaderBuildArtifact: "gbl_x86_64_prod.efi",
		KernelBuildBranch:             "aosp_kernel-common-android17-6.18",
		KernelBuildTarget:             "kernel_virt_x86_64",
	})
	want := []string{
		"cvd",
		"fetch",
		"--default_build=git_main/aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		"--target_directory=/tmp/dir",
		"--android_efi_loader_build=aosp_uefi-gbl-mainline/gbl_efi_dist_and_test{gbl_x86_64_prod.efi}",
		"--bootloader_build=aosp_u-boot-mainline/u-boot_crosvm_x86_64",
		"--kernel_build=aosp_kernel-common-android17-6.18/kernel_virt_x86_64",
		"--credential_source=gce",
	}
	if !slices.Equal(got, want) {
		t.Fatalf("buildFetchCmd() =\n%v\nwant:\n%v", got, want)
	}
}

func TestParseFetcherConfigBuildID(t *testing.T) {
	payload := []byte(`{
		"cvd_files": {
			"/tmp/bootloader": {
				"source": "bootloader_build",
				"build_id": "17012356",
				"build_target": "u-boot_crosvm_x86_64"
			},
			"/tmp/kernel": {
				"source": "kernel_build",
				"build_id": "17005616",
				"build_target": "kernel_virt_x86_64"
			}
		}
	}`)
	got, err := ParseFetcherConfigBuildID(payload, "kernel_build")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got != "17005616" {
		t.Fatalf("got build_id %q, want %q", got, "17005616")
	}
	if _, err := ParseFetcherConfigBuildID(payload, "missing_source"); err == nil {
		t.Fatalf("expected error for missing source")
	}
}
