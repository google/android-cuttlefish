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
	"os"
	"os/exec"
	"testing"
	"time"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
)

func TestInjectInputDeviceEventsActionSucceeds(t *testing.T) {
	tempFile, err := os.CreateTemp("", "test_events_*.bin")
	if err != nil {
		t.Fatal(err)
	}
	tempFilePath := tempFile.Name()
	tempFile.Close()

	var executedArgs []string
	execContext := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		executedArgs = args
		return exec.Command("true")
	}

	om := NewMapOM()
	opts := InjectInputDeviceEventsActionOpts{
		Selector:         cvd.InstanceSelector{GroupName: "foo", Name: "1"},
		DeviceName:       "keyboard",
		EventsFilePath:   tempFilePath,
		OperationManager: om,
		ExecContext:      execContext,
	}
	action := NewInjectInputDeviceEventsAction(opts)

	op, err := action.Run()
	if err != nil {
		t.Fatalf("unexpected error running action: %v", err)
	}

	res, err := om.Wait(op.Name, 5*time.Second)
	if err != nil {
		t.Fatalf("unexpected error waiting for operation: %v", err)
	}
	if res.Error != nil {
		t.Fatalf("operation failed: %v", res.Error)
	}

	// Verify command arguments
	expectedSubcommand := false
	for i := 0; i < len(executedArgs)-1; i++ {
		if executedArgs[i] == "event_devices" && executedArgs[i+1] == "inject" {
			expectedSubcommand = true
			break
		}
	}
	if !expectedSubcommand {
		t.Errorf("expected event_devices inject in args: %v", executedArgs)
	}

	deviceFlagFound := false
	for _, arg := range executedArgs {
		if arg == "--device_name=keyboard" {
			deviceFlagFound = true
			break
		}
	}
	if !deviceFlagFound {
		t.Errorf("expected --device_name=keyboard in args: %v", executedArgs)
	}

	if len(executedArgs) > 0 && executedArgs[len(executedArgs)-1] != tempFilePath {
		t.Errorf("expected events file path %q as last arg, got %q", tempFilePath, executedArgs[len(executedArgs)-1])
	}

	// Verify the temporary file was deleted once cvd is done with it
	if _, err := os.Stat(tempFilePath); !os.IsNotExist(err) {
		t.Errorf("expected temporary events file to be deleted, but it still exists")
	}
}

func TestInjectInputDeviceEventsActionFails(t *testing.T) {
	tempFile, err := os.CreateTemp("", "test_events_*.bin")
	if err != nil {
		t.Fatal(err)
	}
	tempFilePath := tempFile.Name()
	tempFile.Close()

	execContext := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		return exec.Command("false")
	}

	om := NewMapOM()
	opts := InjectInputDeviceEventsActionOpts{
		Selector:         cvd.InstanceSelector{GroupName: "foo", Name: "1"},
		DeviceName:       "mouse",
		EventsFilePath:   tempFilePath,
		OperationManager: om,
		ExecContext:      execContext,
	}
	action := NewInjectInputDeviceEventsAction(opts)

	op, err := action.Run()
	if err != nil {
		t.Fatalf("unexpected error running action: %v", err)
	}

	res, err := om.Wait(op.Name, 5*time.Second)
	if err != nil {
		t.Fatalf("unexpected error waiting for operation: %v", err)
	}
	if res.Error == nil {
		t.Fatal("expected operation error, got nil")
	}
}

func TestInjectInputDeviceEventsActionValidationFails(t *testing.T) {
	om := NewMapOM()
	execContext := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		return exec.Command("true")
	}

	// Empty device name
	opts := InjectInputDeviceEventsActionOpts{
		Selector:         cvd.InstanceSelector{GroupName: "foo", Name: "1"},
		DeviceName:       "",
		EventsFilePath:   "/tmp/foo",
		OperationManager: om,
		ExecContext:      execContext,
	}
	_, err := NewInjectInputDeviceEventsAction(opts).Run()
	if err == nil {
		t.Fatal("expected error for empty device name, got nil")
	}

	// Empty group name
	opts = InjectInputDeviceEventsActionOpts{
		Selector:         cvd.InstanceSelector{GroupName: "", Name: "1"},
		DeviceName:       "mouse",
		EventsFilePath:   "/tmp/foo",
		OperationManager: om,
		ExecContext:      execContext,
	}
	_, err = NewInjectInputDeviceEventsAction(opts).Run()
	if err == nil {
		t.Fatal("expected error for empty group name, got nil")
	}
}
