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

package cli

import (
	"flag"
	"testing"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/nvidia"
)

func TestNvidiaFlagsToResolveOpts(t *testing.T) {
	tests := []struct {
		name        string
		args        []string
		wantAny     bool
		wantVersion string
		wantBranch  string
		wantErr     bool
	}{
		{name: "defaults", wantBranch: nvidia.BranchGCPLTS},
		{name: "explicit_version", args: []string{"-nvidia-version=580.196.11"}, wantAny: true, wantVersion: "580.196.11", wantBranch: nvidia.BranchGCPLTS},
		{name: "version_and_branch_conflict", args: []string{"-nvidia-version=580.196.11", "-nvidia-branch=620"}, wantAny: true, wantErr: true},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			fs := flag.NewFlagSet(tc.name, flag.ContinueOnError)
			var nf NvidiaFlags
			nf.Register(fs)
			if err := fs.Parse(tc.args); err != nil {
				t.Fatalf("fs.Parse(%v) failed: %v", tc.args, err)
			}
			if gotAny := nf.AnyExplicit(fs); gotAny != tc.wantAny {
				t.Errorf("AnyExplicit(%v) = %v, want %v", tc.args, gotAny, tc.wantAny)
			}
			got, err := nf.ToResolveOpts(fs, gce.ArchX86)
			if (err != nil) != tc.wantErr {
				t.Fatalf("ToResolveOpts(%v) err = %v, wantErr = %v", tc.args, err, tc.wantErr)
			}
			if !tc.wantErr && (got.Version != tc.wantVersion || got.Branch != tc.wantBranch) {
				t.Errorf("ToResolveOpts(%v) = %+v, want version=%q branch=%q",
					tc.args, got, tc.wantVersion, tc.wantBranch)
			}
		})
	}
}

func TestNvidiaLabelsRoundTrip(t *testing.T) {
	labels := NvidiaLabels(&nvidia.Driver{Version: "580.196.11"})
	if got, ok := NvidiaVersionFromLabels(labels); !ok || got != "580.196.11" {
		t.Errorf("NvidiaVersionFromLabels(%v) = (%q, %v), want (580.196.11, true)", labels, got, ok)
	}
	if _, ok := NvidiaVersionFromLabels(nil); ok {
		t.Errorf("NvidiaVersionFromLabels(nil) = ok=true, want false")
	}
}
