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
	"fmt"
	"strings"
	"testing"
	"time"

	e2etests "github.com/google/android-cuttlefish/e2etests/cvd/common"
)

const (
	defaultIPv6Timeout = 30 * time.Second
	pollInterval       = 2 * time.Second
)

// checkHostIPv6Support verifies the test host has IPv6 enabled on cuttlefish bridges.
// Skips the test if the host is running in an IPv4-only environment.
func checkHostIPv6Support(t *testing.T, c *e2etests.TestContext) {
	out, err := c.RunCmd("sh", "-c", "ip -6 addr show dev cvd-ebr 2>/dev/null || ip -6 addr show dev cvd-wbr 2>/dev/null || true")
	if err != nil || (!strings.Contains(out.Stdout, "fd00:cf:24:") && !strings.Contains(out.Stdout, "fd00:cf:22:")) {
		t.Skip("Host cuttlefish bridge does not have IPv6 configured; skipping IPv6 tests on IPv4-only host")
	}
}

// pollIPv6Address polls the guest network interfaces until an expected ULA prefix is assigned via SLAAC.
func pollIPv6Address(c *e2etests.TestContext, t *testing.T, timeout time.Duration) (dev string, guestIP string, gatewayIP string, err error) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		out, runErr := c.RunCmd("adb", "shell", "ip -6 -o addr show scope global 2>/dev/null || true")
		if runErr == nil && out.Stdout != "" {
			for _, line := range strings.Split(out.Stdout, "\n") {
				fields := strings.Fields(line)
				if len(fields) >= 4 {
					addrWithMask := fields[3]
					if strings.Contains(addrWithMask, "fd00:cf:24:") {
						return fields[1], strings.Split(addrWithMask, "/")[0], "fd00:cf:24::1", nil
					} else if strings.Contains(addrWithMask, "fd00:cf:22:") {
						return fields[1], strings.Split(addrWithMask, "/")[0], "fd00:cf:22::1", nil
					}
				}
			}
		}
		time.Sleep(pollInterval)
	}
	return "", "", "", fmt.Errorf("timed out after %v waiting for guest SLAAC IPv6 assignment", timeout)
}

// configureGuestIPv6Routes installs explicit on-link routing rules for the assigned ULA prefix.
func configureGuestIPv6Routes(c *e2etests.TestContext, dev, gatewayIP string) error {
	prefix := "fd00:cf:24::/64"
	if strings.Contains(gatewayIP, "22") {
		prefix = "fd00:cf:22::/64"
	}
	cmds := []string{
		"su 0 ip -6 rule add pref 50 lookup main 2>/dev/null || true",
		"su 0 ip -4 rule add pref 50 lookup main 2>/dev/null || true",
		fmt.Sprintf("su 0 ip -6 route add %s dev %s 2>/dev/null || true", prefix, dev),
		fmt.Sprintf("su 0 ip -6 route add default via %s dev %s 2>/dev/null || true", gatewayIP, dev),
	}
	for _, cmd := range cmds {
		if _, err := c.RunCmd("adb", "shell", cmd); err != nil {
			return fmt.Errorf("failed running %q: %w", cmd, err)
		}
	}
	return nil
}

// pingIPv6Target sends ICMPv6 echo requests to targetIP with a 30s timeout and verifies 0% packet loss.
func pingIPv6Target(c *e2etests.TestContext, t *testing.T, dev, guestIP, targetIP string, timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		res, err := c.RunCmd("adb", "shell", fmt.Sprintf("su 0 toybox ping -6 -c 3 -I %s %s", dev, targetIP))
		if err == nil && strings.Contains(res.Stdout, "0% packet loss") {
			t.Logf("Successfully pinged IPv6 target %s from %s", targetIP, dev)
			return true
		}
		time.Sleep(pollInterval)
	}
	return false
}

func TestIPv6DualStackConnectivity(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	checkHostIPv6Support(t, &c)

	t.Log("Fetching Cuttlefish artifacts...")
	if _, err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: "aosp-android-latest-release",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
	}); err != nil {
		t.Fatal(err)
	}

	t.Log("Launching Cuttlefish instance...")
	if _, err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
		t.Fatal(err)
	}

	t.Log("Waiting for ADB connection...")
	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatal(err)
	}

	t.Log("Verifying in-guest SLAAC IPv6 assignment...")
	dev, guestIP, gatewayIP, err := pollIPv6Address(&c, t, defaultIPv6Timeout)
	if err != nil {
		logDiagnostics(&c, t)
		t.Fatalf("Failed to obtain IPv6 SLAAC address: %v", err)
	}
	t.Logf("Acquired IPv6 ULA address %s on device %s (gateway %s)", guestIP, dev, gatewayIP)

	if err := configureGuestIPv6Routes(&c, dev, gatewayIP); err != nil {
		t.Fatalf("Failed configuring guest routes: %v", err)
	}

	t.Logf("Verifying ICMPv6 reachability to gateway %s...", gatewayIP)
	if !pingIPv6Target(&c, t, dev, guestIP, gatewayIP, defaultIPv6Timeout) {
		logDiagnostics(&c, t)
		t.Fatalf("Failed to ping IPv6 gateway %s", gatewayIP)
	}
	t.Log("IPv6 dual-stack connectivity verified successfully.")
}

func TestIPv6OnlyMode(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	checkHostIPv6Support(t, &c)

	t.Log("Fetching Cuttlefish artifacts...")
	if _, err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: "aosp-android-latest-release",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
	}); err != nil {
		t.Fatal(err)
	}

	t.Log("Launching Cuttlefish instance...")
	if _, err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
		t.Fatal(err)
	}

	t.Log("Waiting for ADB connection...")
	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatal(err)
	}

	dev, guestIP, gatewayIP, err := pollIPv6Address(&c, t, defaultIPv6Timeout)
	if err != nil {
		t.Fatalf("Failed to obtain IPv6 address before IPv4 flush: %v", err)
	}

	if err := configureGuestIPv6Routes(&c, dev, gatewayIP); err != nil {
		t.Fatalf("Failed configuring guest routes: %v", err)
	}

	t.Logf("Flushing all IPv4 addresses on %s to enter IPv6-only mode...", dev)
	if _, err := c.RunCmd("adb", "shell", fmt.Sprintf("su 0 ip -4 addr flush dev %s", dev)); err != nil {
		t.Fatalf("Failed to flush IPv4 address: %v", err)
	}
	c.RunCmd("adb", "shell", "su 0 ip -4 route flush table all")

	out, _ := c.RunCmd("adb", "shell", fmt.Sprintf("ip -4 addr show dev %s", dev))
	if strings.Contains(out.Stdout, "inet ") {
		t.Fatalf("IPv4 address still present after flush: %s", out.Stdout)
	}

	t.Logf("Verifying IPv6-only communication to gateway %s...", gatewayIP)
	if !pingIPv6Target(&c, t, dev, guestIP, gatewayIP, defaultIPv6Timeout) {
		logDiagnostics(&c, t)
		t.Fatalf("IPv6-only gateway ping failed")
	}
	t.Log("Pure IPv6 operation verified successfully.")
}
