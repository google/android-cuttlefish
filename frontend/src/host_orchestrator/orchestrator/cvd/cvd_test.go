// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package cvd

import (
	"context"
	"os/exec"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestSliceItoa(t *testing.T) {
	tests := []struct {
		in  []uint32
		out []string
	}{
		{
			in:  []uint32{},
			out: []string{},
		},
		{
			in:  []uint32{79, 83, 89, 97},
			out: []string{"79", "83", "89", "97"},
		},
	}
	for _, tc := range tests {

		res := sliceItoa(tc.in)

		if diff := cmp.Diff(tc.out, res); diff != "" {
			t.Errorf("result mismatch (-want +got):\n%s", diff)
		}
	}
}

func TestInstanceStartArgs(t *testing.T) {
	tests := []struct {
		name     string
		opts     StartOptions
		wantArgs []string
	}{
		{
			name: "default options",
			opts: StartOptions{},
			wantArgs: []string{
				"--group_name=cvd_1",
				"--instance_name=instance2",
				"start",
			},
		},
		{
			name: "with snapshot path",
			opts: StartOptions{SnapshotPath: "/snapshots/snap_1"},
			wantArgs: []string{
				"--group_name=cvd_1",
				"--instance_name=instance2",
				"start",
				"--snapshot_path",
				"/snapshots/snap_1",
			},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			var gotBin string
			var gotArgs []string
			execCtx := func(ctx context.Context, name string, args ...string) *exec.Cmd {
				gotBin = name
				gotArgs = append([]string(nil), args...)
				return exec.CommandContext(ctx, "true")
			}
			cli := NewCLI(execCtx)
			ins := cli.LazySelectInstance(InstanceSelector{
				GroupName: "cvd_1",
				Name:      "instance2",
			})

			if err := ins.Start(tc.opts); err != nil {
				t.Fatalf("Start failed: %v", err)
			}
			if diff := cmp.Diff(CVDBin, gotBin); diff != "" {
				t.Errorf("binary mismatch (-want +got):\n%s", diff)
			}
			if diff := cmp.Diff(tc.wantArgs, gotArgs); diff != "" {
				t.Errorf("args mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

func TestGroupStartArgs(t *testing.T) {
	var gotBin string
	var gotArgs []string
	execCtx := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		gotBin = name
		gotArgs = append([]string(nil), args...)
		return exec.CommandContext(ctx, "true")
	}
	cli := NewCLI(execCtx)
	group := cli.LazySelectGroup(GroupSelector{Name: "cvd_1"})

	if err := group.Start(StartOptions{}); err != nil {
		t.Fatalf("Start failed: %v", err)
	}
	if diff := cmp.Diff(CVDBin, gotBin); diff != "" {
		t.Errorf("binary mismatch (-want +got):\n%s", diff)
	}
	wantArgs := []string{"--group_name=cvd_1", "start"}
	if diff := cmp.Diff(wantArgs, gotArgs); diff != "" {
		t.Errorf("args mismatch (-want +got):\n%s", diff)
	}
}
