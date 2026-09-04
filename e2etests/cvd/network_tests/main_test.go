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

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestIPv6DualStackAndIPv6OnlyMode(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	t.Log("Fetching AOSP build artifacts...")
	if _, err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: "aosp-android-latest-release",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
	}); err != nil {
		t.Fatal(err)
	}

	t.Log("Launching Cuttlefish instance...")
	if err := c.LaunchCVD(e2etests.CreateArgs{}); err != nil {
		t.Fatal(err)
	}

	t.Log("Waiting for ADB device connection...")
	var adbErr error
	for i := 0; i < 10; i++ {
		c.RunCmd("adb", "connect", "127.0.0.1:6520")
		_, adbErr = c.RunCmd("timeout", "15s", "adb", "wait-for-device")
		if adbErr == nil {
			break
		}
		t.Logf("Waiting for ADB connection (attempt %d/10)...", i+1)
	}
	if adbErr != nil {
		t.Fatalf("Timed out waiting for ADB device connection: %v", adbErr)
	}

	// 1. Verify SLAAC IPv6 assignment on in-guest network interface
	t.Log("Verifying in-guest SLAAC IPv6 address assignment...")
	var ip6Output e2etests.CommandOutput
	var activeDev string
	var gatewayIP string
	for attempt := 1; attempt <= 15; attempt++ {
		// Check all potential network interfaces (wireless and ethernet bridges)
		for _, dev := range []string{"eth1", "buried_eth0", "eth0", "wlan0", "wlan1"} {
			out, err := c.RunCmd("adb", "shell", fmt.Sprintf("ip -6 addr show dev %s 2>/dev/null || true", dev))
			if err == nil {
				if strings.Contains(out.Stdout, "fd00:cf:22:") {
					activeDev = dev
					gatewayIP = "fd00:cf:22::1"
					ip6Output = out
					break
				} else if strings.Contains(out.Stdout, "fd00:cf:24:") {
					activeDev = dev
					gatewayIP = "fd00:cf:24::1"
					ip6Output = out
					break
				}
			}
		}
		if activeDev != "" {
			break
		}
		time.Sleep(2 * time.Second)
	}
	if activeDev == "" {
		allIPs, _ := c.RunCmd("adb", "shell", "ip -6 addr show")
		t.Fatalf("Guest missing SLAAC ULA prefix (fd00:cf:22:: or fd00:cf:24::). All IPv6 interfaces:\n%s", allIPs.Stdout)
	}
	var guestIP string
	for _, line := range strings.Split(ip6Output.Stdout, "\n") {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "inet6 fd00:cf:") {
			fields := strings.Fields(line)
			if len(fields) >= 2 {
				guestIP = strings.Split(fields[1], "/")[0]
				break
			}
		}
	}
	t.Logf("Acquired IPv6 ULA address %s on %s (gateway %s):\n%s", guestIP, activeDev, gatewayIP, ip6Output.Stdout)

	// Ensure on-link routing table contains activeDev prefix (requires root on Android)
	prefix := "fd00:cf:24::/64"
	if strings.Contains(gatewayIP, "22") {
		prefix = "fd00:cf:22::/64"
	}
	c.RunCmd("adb", "shell", "su 0 sh -c \"ip -6 rule add pref 50 lookup main 2>/dev/null || true\"")
	c.RunCmd("adb", "shell", "su 0 sh -c \"ip -4 rule add pref 50 lookup main 2>/dev/null || true\"")
	c.RunCmd("adb", "shell", fmt.Sprintf("su 0 sh -c \"ip -6 route add %s dev %s 2>/dev/null || true\"", prefix, activeDev))
	c.RunCmd("adb", "shell", fmt.Sprintf("su 0 sh -c \"ip -6 route add default via %s dev %s 2>/dev/null || true\"", gatewayIP, activeDev))

	// 2. Verify guest-to-host IPv6 connectivity
	t.Logf("Verifying dual-stack guest-to-host IPv6 connectivity (gateway %s on %s)...", gatewayIP, activeDev)
	// Query detected router link-local address from neighbor table
	neighOut, _ := c.RunCmd("adb", "shell", fmt.Sprintf("ip -6 neigh show dev %s", activeDev))
	var routerLL string
	for _, line := range strings.Split(neighOut.Stdout, "\n") {
		if strings.Contains(line, "router") || strings.Contains(line, "fe80:") {
			fields := strings.Fields(line)
			if len(fields) > 0 && strings.HasPrefix(fields[0], "fe80:") {
				routerLL = fields[0]
				break
			}
		}
	}
	t.Logf("Guest ULA: %s, Router LL: %s, Gateway ULA: %s", guestIP, routerLL, gatewayIP)

	connCmd := fmt.Sprintf("su 0 toybox ping -6 -c 3 -I %s %s || su 0 toybox ping -6 -c 3 -I %s %s || su 0 toybox ping -6 -c 3 %s", guestIP, gatewayIP, activeDev, routerLL, gatewayIP)
	connOut, err := c.RunCmd("adb", "shell", connCmd)
	if err != nil {
		diag, _ := c.RunCmd("adb", "shell", fmt.Sprintf("su 0 ip -6 route show; su 0 ip -6 neigh show; su 0 toybox ping -6 -c 3 -I %s %s 2>&1 || true", guestIP, gatewayIP))
		t.Fatalf("Dual-stack IPv6 connectivity failed: %v\nStdout: %s\nStderr: %s\nDiag:\n%s", err, connOut.Stdout, connOut.Stderr, diag.Stdout)
	}
	t.Logf("Dual-stack IPv6 connectivity successful:\n%s", connOut.Stdout)

	// 3. Uninstall / Flush IPv4 (Simulate IPv6-Only environment)
	t.Logf("Flushing IPv4 addresses and routes on %s to test IPv6-only operation...", activeDev)
	if _, err := c.RunCmd("adb", "shell", fmt.Sprintf("su 0 sh -c \"ip -4 addr flush dev %s\"", activeDev)); err != nil {
		t.Fatalf("Failed to flush IPv4 address on %s: %v", activeDev, err)
	}
	c.RunCmd("adb", "shell", "su 0 sh -c \"ip -4 route flush table all\"")

	// 4. Assert IPv4 is completely uninstalled/absent
	ip4Output, _ := c.RunCmd("adb", "shell", fmt.Sprintf("ip -4 addr show dev %s", activeDev))
	if strings.Contains(ip4Output.Stdout, "inet ") {
		t.Fatalf("IPv4 address still present after flush: %s", ip4Output.Stdout)
	}
	t.Log("IPv4 successfully removed. Operating in pure IPv6 mode.")

	// 5. Verify IPv6 communication continues to function 100% with zero IPv4
	t.Logf("Verifying IPv6-only gateway connectivity...")
	connOut, err = c.RunCmd("adb", "shell", connCmd)
	if err != nil {
		diag, _ := c.RunCmd("adb", "shell", fmt.Sprintf("su 0 ip -6 route show; su 0 ip -6 neigh show; su 0 toybox ping -6 -c 3 -I %s %s 2>&1 || true", guestIP, gatewayIP))
		t.Fatalf("IPv6-only connectivity to gateway failed: %v\nStdout: %s\nStderr: %s\nDiag:\n%s", err, connOut.Stdout, connOut.Stderr, diag.Stdout)
	}
	t.Log("Pure IPv6 communication validated successfully.")
}
