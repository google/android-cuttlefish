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
	"net"
	"strings"
	"testing"
	"time"

	e2etests "github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestDeviceNetworking(t *testing.T) {
	testcases := []struct {
		branch string
		target string
	}{
		{
			branch: "aosp-android-latest-release",
			target: "aosp_cf_x86_64_only_phone-userdebug",
		},
	}
	c := e2etests.TestContext{}
	for _, tc := range testcases {
		t.Run(fmt.Sprintf("BUILD=%s/%s", tc.branch, tc.target), func(t *testing.T) {
			c.SetUp(t)
			defer c.TearDown()

			t.Log("Fetching remote build...")
			if err := c.CVDFetch(e2etests.FetchArgs{
				DefaultBuildBranch: tc.branch,
				DefaultBuildTarget: tc.target,
			}); err != nil {
				t.Fatal(err)
			}

			t.Log("Launching Cuttlefish...")
			if err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
				t.Fatal(err)
			}

			t.Log("Waiting for device via ADB...")
			if err := c.RunAdbWaitForDevice(); err != nil {
				t.Fatal(err)
			}

			t.Log("Discovering host IP to ping...")
			hostIP := discoverPingableHostIP(t)
			if hostIP == "" {
				t.Fatal("Could not find a host IP to test connectivity")
			}

			t.Logf("Polling for network connectivity to host IP %s (up to 60s)...", hostIP)
			if !pollPing(&c, t, hostIP) {
				logDiagnostics(&c, t)
				t.Fatal("Failed to establish network connectivity to host")
			}
			t.Log("Network is working successfully on Cuttlefish!")
		})
	}
}

// Polls the guest to ping the host IP until success or timeout (60s).
func pollPing(c *e2etests.TestContext, t *testing.T, hostIP string) bool {
	for i := 0; i < 12; i++ {
		res, err := c.RunCmd("adb", "shell", "ping", "-c", "3", hostIP)
		if err == nil && strings.Contains(res.Stdout, "0% packet loss") {
			t.Logf("Successfully pinged host IP %s", hostIP)
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

// Discover an IP address of the host that the guest can ping to verify default routing.
func discoverPingableHostIP(t *testing.T) string {
	interfaces, err := net.Interfaces()
	if err != nil {
		t.Logf("failed to list host interfaces: %v", err)
		return ""
	}

	for _, ief := range interfaces {
		// Skip cuttlefish bridges (cvd-ebr, cvd-mtap-XX, etc.) and loopback (lo).
		// We want a real external interface IP so that pinging it from the guest
		// forces the guest kernel to use the default route.
		if strings.HasPrefix(ief.Name, "cvd-") || ief.Name == "lo" {
			continue
		}

		addrs, err := ief.Addrs()
		if err != nil {
			continue
		}

		for _, addr := range addrs {
			if ipnet, ok := addr.(*net.IPNet); ok {
				ip := ipnet.IP

				// Filter out loopback (127.0.0.1) and link-local unicast (169.254.x.x) addresses.
				// We need a globally or locally routable IP that the guest can reach via the gateway.
				if !ip.IsLoopback() && !ip.IsLinkLocalUnicast() {
					// Look for a valid IPv4 address.
					if ipv4 := ip.To4(); ipv4 != nil {
						t.Logf("Discovered host primary IP %s on interface %s", ipv4.String(), ief.Name)
						return ipv4.String()
					}
				}
			}
		}
	}

	t.Log("Failed to discover any suitable host IP for ping test")
	return ""
}
