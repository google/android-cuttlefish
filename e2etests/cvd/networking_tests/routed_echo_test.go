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
	"flag"
	"fmt"
	"net/netip"
	"os/exec"
	"slices"
	"strings"
	"testing"
	"time"

	e2etests "github.com/google/android-cuttlefish/e2etests/cvd/common"
)

// IPv6 routed mode echo test. See TestIPv6RoutedEcho.
var (
	routedEchoURL = flag.String("routed_echo_url", "",
		"HTTP(S) URL of a server that answers with the caller's IP address in "+
			"the response body (for example \"2001:db8:cf00:2101::2\"). Setting it "+
			"enables TestIPv6RoutedEcho. Prefer an IPv6 literal host so the "+
			"request cannot fall back to IPv4.")
	routedEchoWifiIface = flag.String("routed_echo_wifi_iface", "wlan0",
		"Guest Wi-Fi interface used by TestIPv6RoutedEcho.")
	routedEchoMobileIface = flag.String("routed_echo_mobile_iface", "buried_eth0",
		"Guest mobile data interface used by TestIPv6RoutedEcho.")
)

// Routed mode networks: the fourth hextet of an instance prefix is
// <network><instance as 2 hex digits> (ipv6_routed_prefix in
// /etc/default/cuttlefish-host-resources).
const (
	routedMobileNet  = 0x21
	routedWifiLanNet = 0x25
)

// routedPrefixFromHostAddr returns the routed /48 of a host address on
// cvd-mtap-<num> in routed mode (P:21<num>::1), or an error when addr does not
// follow the routed mode layout.
func routedPrefixFromHostAddr(addr netip.Addr, num int) (netip.Prefix, error) {
	if !addr.Is6() || addr.Is4In6() || addr.IsPrivate() || !addr.IsGlobalUnicast() {
		return netip.Prefix{}, fmt.Errorf("%s is not a global IPv6 address", addr)
	}
	b := addr.As16()
	if b[6] != routedMobileNet || int(b[7]) != num {
		return netip.Prefix{}, fmt.Errorf("%s is not in P:%02x%02x::/64", addr, routedMobileNet, num)
	}
	return netip.PrefixFrom(addr, 48).Masked(), nil
}

// routedInstancePrefix returns P:<net><num>::/64 of the routed /48 p.
func routedInstancePrefix(p netip.Prefix, net byte, num int) netip.Prefix {
	b := p.Addr().As16()
	b[6], b[7] = net, byte(num)
	return netip.PrefixFrom(netip.AddrFrom16(b), 64)
}

// echoAddr returns the first IP address found in an echo server response.
// It accepts a bare address, or an address among other text or JSON.
func echoAddr(body string) (netip.Addr, error) {
	fields := strings.FieldsFunc(body, func(r rune) bool {
		return !(r == ':' || r == '.' || r == '%' ||
			(r >= '0' && r <= '9') || (r >= 'a' && r <= 'f') || (r >= 'A' && r <= 'F'))
	})
	for _, f := range fields {
		if a, err := netip.ParseAddr(f); err == nil {
			return a.WithZone(""), nil
		}
	}
	return netip.Addr{}, fmt.Errorf("no IP address in echo response %q", body)
}

// hostGlobalIPv6 returns the global IPv6 addresses of a host interface.
func hostGlobalIPv6(ifname string) ([]netip.Addr, error) {
	out, err := exec.Command("ip", "-6", "-o", "addr", "show", "dev", ifname, "scope", "global").CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("ip addr show dev %s: %v: %s", ifname, err, strings.TrimSpace(string(out)))
	}
	return globalIPv6Addrs(string(out)), nil
}

// guestGlobalIPv6InPrefix waits until iface has at least one global address
// in prefix, and returns all its global addresses in prefix (the SLAAC
// address and any temporary privacy addresses).
func guestGlobalIPv6InPrefix(c *e2etests.TestContext, iface string, prefix netip.Prefix) ([]netip.Addr, error) {
	var addrs []netip.Addr
	_, err := waitForGuestOutput(c, "ip -6 -o addr show dev "+iface+" scope global", func(out string) string {
		addrs = nil
		for _, a := range globalIPv6Addrs(out) {
			if prefix.Contains(a) {
				addrs = append(addrs, a)
			}
		}
		if len(addrs) == 0 {
			return ""
		}
		return "ok"
	})
	return addrs, err
}

// fetchEcho fetches url from the guest over iface only and returns the
// address the echo server saw. curl --interface binds the socket to the
// interface (SO_BINDTODEVICE, which needs root), so Android routes it through
// that network's table, as a per-network socket of an app would be.
func fetchEcho(c *e2etests.TestContext, iface, url string) (netip.Addr, error) {
	var lastErr error
	for i := 0; i < 5; i++ {
		out, err := c.RunCmd("adb", "shell", "curl", "-6", "-sS", "--max-time", "15",
			"--interface", iface, "'"+url+"'")
		if err == nil {
			return echoAddr(out.Stdout)
		}
		lastErr = fmt.Errorf("curl over %s: %v (stdout %q, stderr %q)", iface, err, out.Stdout, out.Stderr)
		time.Sleep(pollInterval)
	}
	return netip.Addr{}, lastErr
}

// TestIPv6RoutedEcho checks the property that CTS
// ConnectivityManagerTest#testOpenConnection needs on each network: when the
// guest fetches an echo URL over Wi-Fi and over mobile data separately, the
// server sees a different address per network, and each seen address is one
// of the guest's own addresses on that interface (no NAT66 on the path).
//
// Requirements, none of which the test sets up:
//   - The host runs cuttlefish-host-resources in IPv6 routed mode
//     (ipv6_routed_prefix=P::/48) and P::/48 is routed to the host.
//   - The server at --routed_echo_url is reachable from P::/48 over IPv6.
//   - A debuggable image (adb root, for curl --interface) that connects Wi-Fi
//     at boot, whose RIL configures IPv6 on the mobile interface and whose
//     OpenWrt reads lan_ip6prefix from the kernel command line.
//   - The instance uses the non-bridged Wi-Fi tap (cvd-wifiap-<i>).
//
// The test is skipped unless --routed_echo_url is set, for example:
//
//	bazel test //cvd/networking_tests:networking_tests \
//	  --test_arg=--routed_echo_url=http://[2001:db8:ec00::80]/ \
//	  --test_arg=-test.run=TestIPv6RoutedEcho
func TestIPv6RoutedEcho(t *testing.T) {
	if *routedEchoURL == "" {
		t.Skip("skipping: set --routed_echo_url to run the routed mode echo test")
	}
	skipUnderPodcvd(t)
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	d := launchDevice(t, &c)
	if d.useBridgedWifiTap {
		t.Skip("skipping: routed mode Wi-Fi needs the non-bridged Wi-Fi tap (cvd-wifiap)")
	}

	mtap := fmt.Sprintf("cvd-mtap-%02d", d.num)
	hostAddrs, err := hostGlobalIPv6(mtap)
	if err != nil {
		t.Fatal(err)
	}
	var routed netip.Prefix
	for _, a := range hostAddrs {
		if p, err := routedPrefixFromHostAddr(a, d.num); err == nil {
			routed = p
			break
		}
	}
	if !routed.IsValid() {
		t.Fatalf("host %s has no routed mode address (P:%02x%02x::1/64), got %v; is ipv6_routed_prefix set?",
			mtap, routedMobileNet, d.num, hostAddrs)
	}
	t.Logf("routed prefix %s", routed)

	if out, err := guestShell(&c, "getprop ro.debuggable"); err != nil || strings.TrimSpace(out) != "1" {
		t.Skipf("skipping: curl --interface needs adb root on a debuggable image (ro.debuggable=%q, %v)", strings.TrimSpace(out), err)
	}
	if _, err := c.RunCmd("adb", "root"); err != nil {
		t.Fatal(err)
	}
	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatal(err)
	}

	networks := []struct {
		name, iface string
		prefix      netip.Prefix
	}{
		{"mobile", *routedEchoMobileIface, routedInstancePrefix(routed, routedMobileNet, d.num)},
		{"wifi", *routedEchoWifiIface, routedInstancePrefix(routed, routedWifiLanNet, d.num)},
	}
	seen := map[string]netip.Addr{}
	for _, n := range networks {
		own, err := guestGlobalIPv6InPrefix(&c, n.iface, n.prefix)
		if err != nil {
			t.Errorf("%s (%s): no address in %s: %v", n.name, n.iface, n.prefix, err)
			continue
		}
		got, err := fetchEcho(&c, n.iface, *routedEchoURL)
		if err != nil {
			t.Errorf("%s (%s): %v", n.name, n.iface, err)
			continue
		}
		t.Logf("%s (%s): server saw %s, guest addresses %v", n.name, n.iface, got, own)
		if !slices.Contains(own, got) {
			t.Errorf("%s (%s): server saw %s, want one of the guest's own addresses %v (NAT on the path?)",
				n.name, n.iface, got, own)
		}
		seen[n.name] = got
	}
	if m, w := seen["mobile"], seen["wifi"]; m.IsValid() && w.IsValid() && m == w {
		t.Errorf("server saw the same address %s over mobile and Wi-Fi", m)
	}
}
