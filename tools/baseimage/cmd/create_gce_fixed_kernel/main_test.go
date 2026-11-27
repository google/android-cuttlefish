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
	"flag"
	"testing"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/cli"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
)

func TestValidateLinuxImageDeb(t *testing.T) {
	tests := []struct {
		name    string
		pkg     string
		arch    gce.Arch
		wantErr bool
	}{
		{name: "valid_x86", pkg: "linux-image-6.12.118+deb13-cloud-amd64", arch: gce.ArchX86},
		{name: "valid_arm64", pkg: "linux-image-7.3.4+deb13-cloud-arm64", arch: gce.ArchArm},
		{name: "metapackage", pkg: "linux-image-cloud-amd64", arch: gce.ArchX86, wantErr: true},
		{name: "arm64_on_x86", pkg: "linux-image-6.12.118+deb13-cloud-arm64", arch: gce.ArchX86, wantErr: true},
		{name: "amd64_on_arm64", pkg: "linux-image-6.12.118+deb13-cloud-amd64", arch: gce.ArchArm, wantErr: true},
		{name: "shell_metachars", pkg: "linux-image-6.12.118;id", arch: gce.ArchX86, wantErr: true},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if err := validateLinuxImageDeb(tc.pkg, tc.arch); (err != nil) != tc.wantErr {
				t.Errorf("validateLinuxImageDeb(%q, %v) err = %v, wantErr = %v", tc.pkg, tc.arch, err, tc.wantErr)
			}
		})
	}
}

func TestValidateFlags(t *testing.T) {
	tests := []struct {
		name    string
		args    []string
		wantErr bool
	}{
		{name: "defaults", args: []string{"-project=p", "-arch=x86_64", "-image-name=img"}},
		{name: "explicit_debian12", args: []string{"-project=p", "-arch=arm64", "-debian-version=12", "-image-name=img"}},
		{name: "explicit_kernel", args: []string{"-project=p", "-arch=x86_64", "-linux-image-deb=linux-image-6.12.118+deb13-cloud-amd64", "-image-name=img"}},
		{name: "unsupported_debian", args: []string{"-project=p", "-arch=x86_64", "-debian-version=11", "-image-name=img"}, wantErr: true},
		{name: "kernel_arch_mismatch", args: []string{"-project=p", "-arch=arm64", "-linux-image-deb=linux-image-6.12.118+deb13-cloud-amd64", "-image-name=img"}, wantErr: true},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			arch = cli.Arch("")
			fs := flag.NewFlagSet(tc.name, flag.ContinueOnError)
			registerFlags(fs)
			if err := fs.Parse(tc.args); err != nil {
				t.Fatalf("fs.Parse(%v) failed: %v", tc.args, err)
			}
			if err := validateFlags(); (err != nil) != tc.wantErr {
				t.Errorf("validateFlags(%v) err = %v, wantErr = %v", tc.args, err, tc.wantErr)
			}
		})
	}
}
