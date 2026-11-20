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
	"strconv"
	"strings"
	"testing"
	"time"

	e2etests "github.com/google/android-cuttlefish/e2etests/cvd/common"
)

// Injected at link time via `x_defs` in def.bzl.
var (
	branch string
	target string
	// Kept as a string rather than a bool so an unset value can be detected.
	useCvdalloc string
)

func TestDeviceNetworking(t *testing.T) {
	if branch == "" || target == "" || useCvdalloc == "" {
		t.Fatalf("branch, target and useCvdalloc must be set via x_defs in def.bzl (branch=%q, target=%q, useCvdalloc=%q)", branch, target, useCvdalloc)
	}
	cvdalloc, err := strconv.ParseBool(useCvdalloc)
	if err != nil {
		t.Fatalf("invalid useCvdalloc value %q: %v", useCvdalloc, err)
	}

	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	t.Log("Fetching remote build...")
	if _, err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: branch,
		DefaultBuildTarget: target,
	}); err != nil {
		t.Fatal(err)
	}

	t.Log("Launching Cuttlefish...")
	createArgs := e2etests.CreateArgs{}
	if cvdalloc {
		createArgs.Args = append(createArgs.Args, "--use_cvdalloc=true")
	}
	if _, err := c.CVDCreate(createArgs); err != nil {
		t.Fatal(err)
	}

	t.Log("Waiting for device via ADB...")
	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatal(err)
	}

	pingIP := "8.8.8.8"

	t.Logf("Polling for network connectivity to host IP %s (up to 60s)...", pingIP)
	if !pollPing(&c, t, pingIP) {
		logDiagnostics(&c, t)
		t.Fatal("Failed to establish network connectivity to host")
	}
	t.Log("Network is working successfully on Cuttlefish!")
}

// Polls the guest to ping the host IP until success or timeout (60s).
func pollPing(c *e2etests.TestContext, t *testing.T, pingIP string) bool {
	for i := 0; i < 12; i++ {
		res, err := c.RunCmd("adb", "shell", "ping", "-c", "3", pingIP)
		if err == nil && strings.Contains(res.Stdout, "0% packet loss") {
			t.Logf("Successfully pinged IP %s", pingIP)
			return true
		}
		t.Logf("Ping failed, retrying... (err: %v)", err)
		time.Sleep(5 * time.Second)
	}
	return false
}

// Collects and logs network diagnostics from the guest on failure.
func logDiagnostics(c *e2etests.TestContext, t *testing.T) {
	t.Log("Collecting network diagnostics from guest...")
	if routeRes, err := c.RunCmd("adb", "shell", "ip", "route", "show", "table", "all"); err == nil {
		t.Logf("Routing tables:\n%s", routeRes.Stdout)
	} else {
		t.Logf("failed to get routing tables: %v", err)
	}
	if addrRes, err := c.RunCmd("adb", "shell", "ip", "addr"); err == nil {
		t.Logf("IP Addresses:\n%s", addrRes.Stdout)
	} else {
		t.Logf("failed to get ip addrs: %v", err)
	}
	if ruleRes, err := c.RunCmd("adb", "shell", "ip", "rule"); err == nil {
		t.Logf("IP Rules:\n%s", ruleRes.Stdout)
	} else {
		t.Logf("failed to get ip rules: %v", err)
	}
}
