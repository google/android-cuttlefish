// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
)

func touchFile(t *testing.T, dir, name string) string {
	t.Helper()
	p := filepath.Join(dir, name)
	if err := os.MkdirAll(filepath.Dir(p), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, []byte("test"), 0644); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestFindCuttlefishDebsSuccess(t *testing.T) {
	dir := t.TempDir()
	wantNames := []string{
		"cuttlefish-base_1.55.5_amd64.deb",
		"cuttlefish-defaults_1.55.5_amd64.deb",
		"cuttlefish-integration_1.55.5_amd64.deb",
		"cuttlefish-metrics_1.55.5_amd64.deb",
		"cuttlefish-user_1.55.5_amd64.deb",
		"cuttlefish-orchestration_1.55.5_amd64.deb",
	}
	for _, name := range wantNames {
		touchFile(t, dir, filepath.Join("nested", name))
	}
	// Extra packages (metapackage, dbgsym, other arch) must be ignored.
	touchFile(t, dir, "cuttlefish-common_1.55.5_amd64.deb")
	touchFile(t, dir, "cuttlefish-base-dbgsym_1.55.5_amd64.deb")
	touchFile(t, dir, "cuttlefish-integration-gigabyte-arm64_1.55.5_amd64.deb")
	touchFile(t, dir, "cuttlefish-podcvd_1.55.5_amd64.deb")
	touchFile(t, dir, "cuttlefish-base_1.55.5_arm64.deb")

	got, err := findCuttlefishDebs(dir, gce.ArchX86)
	if err != nil {
		t.Fatalf("findCuttlefishDebs() unexpected error: %v", err)
	}
	if len(got) != len(wantNames) {
		t.Fatalf("len(got) = %d, want %d (%v)", len(got), len(wantNames), got)
	}
	for i, wantName := range wantNames {
		if filepath.Base(got[i]) != wantName {
			t.Errorf("got[%d] base = %q, want %q", i, filepath.Base(got[i]), wantName)
		}
	}
}

func TestFindCuttlefishDebsMissingPackage(t *testing.T) {
	dir := t.TempDir()
	touchFile(t, dir, "cuttlefish-base_1.55.5_amd64.deb")

	_, err := findCuttlefishDebs(dir, gce.ArchX86)
	if err == nil || !strings.Contains(err.Error(), "missing cuttlefish-defaults package") {
		t.Fatalf("findCuttlefishDebs() error = %v, want missing cuttlefish-defaults error", err)
	}
}

func TestFindCuttlefishDebsDuplicatePackage(t *testing.T) {
	dir := t.TempDir()
	for _, pkg := range cuttlefishDebPackages {
		touchFile(t, dir, pkg+"_1.55.5_amd64.deb")
	}
	touchFile(t, dir, "subdir/cuttlefish-base_1.55.4_amd64.deb")

	_, err := findCuttlefishDebs(dir, gce.ArchX86)
	if err == nil || !strings.Contains(err.Error(), "multiple cuttlefish-base packages") {
		t.Fatalf("findCuttlefishDebs() error = %v, want duplicate cuttlefish-base error", err)
	}
}

func TestResolveDebsMutualExclusion(t *testing.T) {
	_, err := resolveDebs("/some/dir", []string{"/some/pkg.deb"}, gce.ArchX86)
	if err == nil || !strings.Contains(err.Error(), "specify either `-debs-dir` or `-deb`, not both") {
		t.Fatalf("resolveDebs() error = %v, want mutual exclusion error", err)
	}
}

func TestDefaultReleaseImageName(t *testing.T) {
	ts := time.Date(2026, 10, 31, 2, 15, 30, 0, time.UTC)
	cases := []struct {
		name string
		deb  string
		want string
	}{
		{
			name: "three-component version with build suffix",
			deb:  "/tmp/debs/cuttlefish-base_1.55.5-829564262_amd64.deb",
			want: "cuttlefish-image-20261031021530--cvd1-55",
		},
		{
			name: "two-component version without patch segment",
			deb:  "/tmp/debs/cuttlefish-base_1.55_amd64.deb",
			want: "cuttlefish-image-20261031021530--cvd1-55",
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got, err := defaultReleaseImageName([]string{tc.deb}, ts)
			if err != nil {
				t.Fatalf("defaultReleaseImageName() unexpected error: %v", err)
			}
			if got != tc.want {
				t.Errorf("defaultReleaseImageName() = %q, want %q", got, tc.want)
			}
		})
	}
}

func TestDefaultReleaseImageNameMissingBaseDeb(t *testing.T) {
	ts := time.Date(2026, 10, 31, 2, 15, 30, 0, time.UTC)
	_, err := defaultReleaseImageName([]string{"/tmp/debs/cuttlefish-user_1.55.5_amd64.deb"}, ts)
	if err == nil {
		t.Fatal("defaultReleaseImageName() expected error when cuttlefish-base is absent, got nil")
	}
}

func TestEnableGceMetadataCmd(t *testing.T) {
	srcConfig := filepath.Join("..", "..", "..", "..", "frontend", "debian", "cuttlefish-orchestration.cuttlefish-host_orchestrator.default")
	raw, err := os.ReadFile(srcConfig)
	if err != nil {
		t.Fatalf("failed to read %s: %v", srcConfig, err)
	}

	tmpDir := t.TempDir()
	hoFile := filepath.Join(tmpDir, "cuttlefish-host_orchestrator")
	if err := os.WriteFile(hoFile, raw, 0644); err != nil {
		t.Fatal(err)
	}

	// Strip "sudo " for local unprivileged test execution.
	cmdStr := strings.ReplaceAll(enableGceMetadataCmd(hoFile), "sudo ", "")
	if out, err := exec.Command("sh", "-c", cmdStr).CombinedOutput(); err != nil {
		t.Fatalf("enableGceMetadataCmd failed: %v, output: %s", err, string(out))
	}

	updated, err := os.ReadFile(hoFile)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(updated), "\nbuild_api_credentials_use_gce_metadata=true\n") {
		t.Errorf("updated config missing uncommented build_api_credentials_use_gce_metadata=true:\n%s", string(updated))
	}
}
