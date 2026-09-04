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

func TestParseListInputDevicesOutput(t *testing.T) {
	tests := []struct {
		name         string
		out          string
		groupName    string
		instanceName string
		want         []string
		wantErr      bool
	}{
		{
			name:         "multiple devices",
			out:          "foo/bar: dev1 dev2 dev3\n",
			groupName:    "foo",
			instanceName: "bar",
			want:         []string{"dev1", "dev2", "dev3"},
		},
		{
			name:         "single device",
			out:          "foo/bar: keyboard\n",
			groupName:    "foo",
			instanceName: "bar",
			want:         []string{"keyboard"},
		},
		{
			name:         "no devices",
			out:          "foo/bar:\n",
			groupName:    "foo",
			instanceName: "bar",
			want:         []string{},
		},
		{
			name:         "no devices with trailing space",
			out:          "foo/bar: \n",
			groupName:    "foo",
			instanceName: "bar",
			want:         []string{},
		},
		{
			name:         "extra spaces between tokens",
			out:          "  foo/bar :   dev1    dev2   \n",
			groupName:    "foo",
			instanceName: "bar",
			want:         []string{"dev1", "dev2"},
		},
		{
			name:         "multi-line with logs and multiple instances",
			out:          "WARNING: some log\nother_group/other_inst: devX devY\nfoo/bar: dev1 dev2\nINFO: finished\n",
			groupName:    "foo",
			instanceName: "bar",
			want:         []string{"dev1", "dev2"},
		},
		{
			name:         "instance not found",
			out:          "other/other: dev1\n",
			groupName:    "foo",
			instanceName: "bar",
			wantErr:      true,
		},
		{
			name:         "empty output",
			out:          "",
			groupName:    "foo",
			instanceName: "bar",
			wantErr:      true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := parseListInputDevicesOutput(tc.out, tc.groupName, tc.instanceName, []string{"dummy"})
			if (err != nil) != tc.wantErr {
				t.Fatalf("wantErr %v, got %v", tc.wantErr, err)
			}
			if tc.wantErr {
				return
			}
			if diff := cmp.Diff(tc.want, got); diff != "" {
				t.Errorf("result mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

func TestInjectInputDeviceEvents(t *testing.T) {
	var capturedArgs []string
	execCtx := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		capturedArgs = args
		return exec.Command("true")
	}
	cli := NewCLI(execCtx)
	inst := cli.LazySelectInstance(InstanceSelector{GroupName: "foo", Name: "1"})
	err := inst.InjectInputDeviceEvents("mouse", "/tmp/events.bin")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	wantArgs := []string{
		"--group_name=foo",
		"--instance_name=1",
		"event_devices",
		"inject",
		"--device_name=mouse",
		"/tmp/events.bin",
	}
	if diff := cmp.Diff(wantArgs, capturedArgs); diff != "" {
		t.Errorf("args mismatch (-want +got):\n%s", diff)
	}
}
