// Copyright 2026 Google LLC
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

package orchestrator

import (
	"context"
	"os/exec"
	"strings"
	"testing"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/go-cmp/cmp"
)

func TestListInputDevicesActionSucceeds(t *testing.T) {
	output := "foo/1: keyboard mouse touchscreen\n"
	execContext := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		cmd := exec.Command("true")
		if len(args) >= 2 && args[len(args)-2] == "event_devices" && args[len(args)-1] == "list" {
			cmd = exec.Command("tee", "/dev/stderr")
			cmd.Stdin = strings.NewReader(output)
		}
		return cmd
	}

	opts := ListInputDevicesActionOpts{
		Selector:    cvd.InstanceSelector{GroupName: "foo", Name: "1"},
		ExecContext: execContext,
	}
	action := NewListInputDevicesAction(opts)

	res, err := action.Run()
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	want := &apiv1.ListInputDevicesResponse{
		InputDevices: []string{"keyboard", "mouse", "touchscreen"},
	}
	if diff := cmp.Diff(want, res); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestListInputDevicesActionEmptyDevices(t *testing.T) {
	output := "foo/1:\n"
	execContext := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		cmd := exec.Command("true")
		if len(args) >= 2 && args[len(args)-2] == "event_devices" && args[len(args)-1] == "list" {
			cmd = exec.Command("tee", "/dev/stderr")
			cmd.Stdin = strings.NewReader(output)
		}
		return cmd
	}

	opts := ListInputDevicesActionOpts{
		Selector:    cvd.InstanceSelector{GroupName: "foo", Name: "1"},
		ExecContext: execContext,
	}
	action := NewListInputDevicesAction(opts)

	res, err := action.Run()
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	want := &apiv1.ListInputDevicesResponse{
		InputDevices: []string{},
	}
	if diff := cmp.Diff(want, res); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestListInputDevicesActionCommandFails(t *testing.T) {
	execContext := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		return exec.Command("false")
	}

	opts := ListInputDevicesActionOpts{
		Selector:    cvd.InstanceSelector{GroupName: "foo", Name: "1"},
		ExecContext: execContext,
	}
	action := NewListInputDevicesAction(opts)

	_, err := action.Run()
	if err == nil {
		t.Fatal("expected error, got nil")
	}
}
