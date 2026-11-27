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

package gce

import "testing"

func TestDebianImageFamily(t *testing.T) {
	tests := []struct {
		name    string
		version int
		arch    Arch
		want    string
		wantErr bool
	}{
		{name: "debian12_x86", version: 12, arch: ArchX86, want: "debian-12"},
		{name: "debian13_x86", version: 13, arch: ArchX86, want: "debian-13"},
		{name: "debian13_arm64", version: 13, arch: ArchArm, want: "debian-13-arm64"},
		{name: "zero_version", version: 0, arch: ArchX86, wantErr: true},
		{name: "unknown_arch", version: 13, arch: ArchUnknown, wantErr: true},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := DebianImageFamily(tc.version, tc.arch)
			if (err != nil) != tc.wantErr {
				t.Fatalf("DebianImageFamily(%d, %v) err = %v, wantErr = %v", tc.version, tc.arch, err, tc.wantErr)
			}
			if !tc.wantErr && got != tc.want {
				t.Errorf("DebianImageFamily(%d, %v) = %q, want %q", tc.version, tc.arch, got, tc.want)
			}
		})
	}
}
