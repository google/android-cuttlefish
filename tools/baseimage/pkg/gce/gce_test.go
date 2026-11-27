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

import (
	"testing"

	"google.golang.org/api/compute/v1"
)

func TestImageMatchesArch(t *testing.T) {
	tests := []struct {
		name string
		arch Arch
		img  *compute.Image
		want bool
	}{
		{name: "x86_matches_x86_64", arch: ArchX86, img: &compute.Image{Architecture: "X86_64"}, want: true},
		{name: "x86_matches_legacy_empty", arch: ArchX86, img: &compute.Image{}, want: true},
		{name: "x86_rejects_arm64", arch: ArchX86, img: &compute.Image{Architecture: "ARM64"}, want: false},
		{name: "arm_matches_arm64", arch: ArchArm, img: &compute.Image{Architecture: "ARM64"}, want: true},
		{name: "arm_rejects_x86_64", arch: ArchArm, img: &compute.Image{Architecture: "X86_64"}, want: false},
		{name: "nil_image", arch: ArchX86, img: nil, want: false},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if got := ImageMatchesArch(tc.arch, tc.img); got != tc.want {
				t.Errorf("ImageMatchesArch(%v, %+v) = %v, want %v", tc.arch, tc.img, got, tc.want)
			}
		})
	}
}
