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

package main

import (
	"testing"

	"github.com/google/android-cuttlefish/base/cvd/host_tests/common"
	"github.com/google/go-cmp/cmp"
)

func killedInstance1State() phaseState {
	state := instance1State()
	state.NftTables = nil
	state.NftChains = nil
	state.Masquerades = nil
	return state
}

// Restarting a killed id must clean up its leftover taps instead of failing.
func TestCvdallocRestartAfterKill(t *testing.T) {
	s := common.NewSandbox(t)
	c := common.NewCvdalloc(t, s)
	base := snapshot(t, s)

	inst, err := c.StartInstance(1)
	if err != nil {
		t.Fatalf("start instance 1: %v", err)
	}
	if err := c.KillInstance(inst); err != nil {
		t.Fatalf("kill instance 1: %v", err)
	}
	if diff := cmp.Diff(killedInstance1State(), observe(t, s), cmpOptions()); diff != "" {
		t.Fatalf("host state after kill (-want +got):\n%s", diff)
	}

	inst, err = c.StartInstance(1)
	if err != nil {
		t.Fatalf("restart instance 1: %v", err)
	}
	if diff := cmp.Diff(instance1State(), observe(t, s), cmpOptions()); diff != "" {
		t.Errorf("host state after restart (-want +got):\n%s", diff)
	}

	if err := c.StopInstance(inst); err != nil {
		t.Fatalf("stop instance 1: %v", err)
	}
	checkReleased(t, s, base)
}

// A killed id that never comes back must not leak its taps.
func TestCvdallocSweepsTapsOfKilledId(t *testing.T) {
	s := common.NewSandbox(t)
	c := common.NewCvdalloc(t, s)
	base := snapshot(t, s)

	inst1, err := c.StartInstance(1)
	if err != nil {
		t.Fatalf("start instance 1: %v", err)
	}
	if err := c.KillInstance(inst1); err != nil {
		t.Fatalf("kill instance 1: %v", err)
	}

	inst2, err := c.StartInstance(2)
	if err != nil {
		t.Fatalf("start instance 2: %v", err)
	}
	if diff := cmp.Diff(onlyInstance2State(), observe(t, s), cmpOptions()); diff != "" {
		t.Errorf("host state after instance 2 swept instance 1 (-want +got):\n%s", diff)
	}

	if err := c.StopInstance(inst2); err != nil {
		t.Fatalf("stop instance 2: %v", err)
	}
	checkReleased(t, s, base)
}

func TestCvdallocRestartsDeadDnsmasqOnReuse(t *testing.T) {
	const bridge = "cvd-pi-ebr"
	s := common.NewSandbox(t)
	c := common.NewCvdalloc(t, s)
	base := snapshot(t, s)

	inst1, err := c.StartInstance(1)
	if err != nil {
		t.Fatalf("start instance 1: %v", err)
	}
	if err := common.KillCvdallocDnsmasq(s, bridge); err != nil {
		t.Fatal(err)
	}
	if common.CvdallocDnsmasqAlive(s, bridge) {
		t.Fatalf("dnsmasq for %s survived SIGKILL", bridge)
	}

	inst2, err := c.StartInstance(2)
	if err != nil {
		t.Fatalf("start instance 2: %v", err)
	}
	if !common.CvdallocDnsmasqAlive(s, bridge) {
		t.Errorf("dnsmasq for %s was not restarted", bridge)
	}
	if diff := cmp.Diff(bothInstancesState(), observe(t, s), cmpOptions()); diff != "" {
		t.Errorf("host state with both instances (-want +got):\n%s", diff)
	}

	if err := c.StopInstance(inst2); err != nil {
		t.Fatalf("stop instance 2: %v", err)
	}
	if err := c.StopInstance(inst1); err != nil {
		t.Fatalf("stop instance 1: %v", err)
	}
	checkReleased(t, s, base)
}

// SIGTERM must tear down, but exit nonzero so it isn't taken for a clean stop.
func TestCvdallocTerminatesOnSignal(t *testing.T) {
	s := common.NewSandbox(t)
	c := common.NewCvdalloc(t, s)
	base := snapshot(t, s)

	inst, err := c.StartInstance(1)
	if err != nil {
		t.Fatalf("start instance 1: %v", err)
	}
	code, err := c.TerminateInstance(inst)
	if err != nil {
		t.Fatal(err)
	}
	if code != 1 {
		t.Errorf("exit code after SIGTERM = %d, want 1", code)
	}
	checkReleased(t, s, base)
}
