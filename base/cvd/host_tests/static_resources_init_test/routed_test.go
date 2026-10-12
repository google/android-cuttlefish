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

// IPv6 routed mode of cuttlefish-host-resources (ipv6_routed_prefix in
// /etc/default/cuttlefish-host-resources): every guest network gets a /64
// from a routed /48, with no NAT66, and the host routes each OpenWrt Wi-Fi
// LAN /64 to that instance's OpenWrt WAN address.

import (
	"fmt"
	"regexp"
	"slices"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

// routedPrefix is the /48 used by the tests (documentation range, RFC 3849).
const (
	routedPrefix = "2001:db8:cf00::/48"
	routedBase   = "2001:db8:cf00"
)

// routedNet returns the routed mode /64 prefix ("P:<net><i>::") of network
// net (21 mobile, 23 OpenWrt WAN, 25 OpenWrt LAN) for instance i.
func routedNet(net, i int) string { return fmt.Sprintf("%s:%d%02x::", routedBase, net, i) }

// lanRoutes returns the IPv6 routes to OpenWrt LAN /64s of the routed prefix,
// one "<prefix> via <gateway> dev <device>" per entry, sorted.
func (f *ipv6Fixture) lanRoutes() []string {
	f.t.Helper()
	var out []string
	re := regexp.MustCompile(`^(\S+) via (\S+) dev (\S+)`)
	for _, line := range strings.Split(f.sh("ip -6 route show"), "\n") {
		m := re.FindStringSubmatch(strings.TrimSpace(line))
		if m == nil || !strings.HasPrefix(m[1], routedBase+":25") {
			continue
		}
		out = append(out, m[1]+" via "+m[2]+" dev "+m[3])
	}
	slices.Sort(out)
	return out
}

func wantLanRoutes(n int) []string {
	var want []string
	for i := 1; i <= n; i++ {
		want = append(want, fmt.Sprintf("%s/64 via %s2 dev %s", routedNet(25, i), routedNet(23, i), tapName("wifiap", i)))
	}
	slices.Sort(want)
	return want
}

// TestRoutedIPv6Addressing checks the routed mode address plan, the RA
// prefixes, the absence of NAT66 and the routes to the OpenWrt LAN /64s,
// with 10 accounts so instance 10 exercises the hex instance number.
func TestRoutedIPv6Addressing(t *testing.T) {
	const n = 10
	f := newIPv6Fixture(t)
	f.writeDefaults(fmt.Sprintf("num_cvd_accounts=%d", n), "ipv6_routed_prefix="+routedPrefix)
	base := f.snapshot()
	f.initScript("start")

	hs := f.snapshot()
	want := map[string][]string{
		"cvd-ebr": {routedBase + ":24::1/64"},
		"cvd-wbr": {routedBase + ":22::1/64"},
	}
	for i := 1; i <= n; i++ {
		want[tapName("mtap", i)] = []string{routedNet(21, i) + "1/64"}
		want[tapName("wifiap", i)] = []string{routedNet(23, i) + "1/64"}
		want[tapName("etap", i)] = nil
		want[tapName("wtap", i)] = nil
	}
	for ifname, w := range want {
		if diff := cmp.Diff(w, globalIPv6(hs, ifname)); diff != "" {
			t.Errorf("global IPv6 of %s (-want +got):\n%s", ifname, diff)
		}
	}

	// RAs as in private mode (bridges and cvd-wifiap-XX), with routed prefixes.
	for ifname, prefix := range map[string]string{
		"cvd-ebr":            routedBase + ":24::",
		"cvd-wbr":            routedBase + ":22::",
		tapName("wifiap", 1): routedNet(23, 1),
		tapName("wifiap", n): routedNet(23, n),
	} {
		cmdline := f.sh(fmt.Sprintf("tr '\\0' ' ' < /proc/$(cat /run/cuttlefish-dnsmasq-ra-%s.pid)/cmdline", ifname))
		if arg := "--dhcp-range=" + prefix + ",ra-only,64"; !strings.Contains(cmdline, arg) {
			t.Errorf("RA dnsmasq on %s lacks %q: %s", ifname, arg, cmdline)
		}
	}
	wantRA := []string{"cvd-ebr", "cvd-wbr"}
	for i := 1; i <= n; i++ {
		wantRA = append(wantRA, tapName("wifiap", i))
	}
	slices.Sort(wantRA)
	if diff := cmp.Diff(wantRA, raDnsmasqIfaces(f.s)); diff != "" {
		t.Errorf("RA dnsmasq interfaces (-want +got):\n%s", diff)
	}

	// No NAT66: the table exists (so stop is the same in both modes), but
	// has no rule.
	if nat6 := f.sh("nft list chain ip6 cuttlefish_nat6 postrouting"); strings.Contains(nat6, "masquerade") {
		t.Errorf("NAT66 rule in routed mode:\n%s", nat6)
	}
	// The RA guard is the same as in private mode.
	inetGuard := f.sh("nft list chain inet cuttlefish_ra_guard input")
	for _, p := range []string{"cvd-mtap-*", "cvd-wifiap-*"} {
		if !guardRuleFor(inetGuard, p) {
			t.Errorf("no RA/redirect drop rule for %s:\n%s", p, inetGuard)
		}
	}

	if diff := cmp.Diff(wantLanRoutes(n), f.lanRoutes()); diff != "" {
		t.Errorf("OpenWrt LAN routes (-want +got):\n%s", diff)
	}

	f.initScript("stop")
	if r := f.lanRoutes(); len(r) != 0 {
		t.Errorf("OpenWrt LAN routes left after stop: %v", r)
	}
	f.requireNoLeak(base)
}

// TestRoutedIPv6ShortPrefix checks a /48 written with fewer than three
// hextets: 2001:db8::/48 is 2001:db8:0::/48.
func TestRoutedIPv6ShortPrefix(t *testing.T) {
	f := newIPv6Fixture(t)
	f.writeDefaults("num_cvd_accounts=1", "ipv6_routed_prefix=2001:DB8::/48")
	base := f.snapshot()
	f.initScript("start")

	hs := f.snapshot()
	for ifname, w := range map[string]string{
		"cvd-ebr":       "2001:db8:0:24::1/64",
		"cvd-mtap-01":   "2001:db8:0:2101::1/64",
		"cvd-wifiap-01": "2001:db8:0:2301::1/64",
	} {
		if diff := cmp.Diff([]string{w}, globalIPv6(hs, ifname)); diff != "" {
			t.Errorf("global IPv6 of %s (-want +got):\n%s", ifname, diff)
		}
	}
	if r := f.sh("ip -6 route show 2001:db8:0:2501::/64"); !strings.Contains(r, "via 2001:db8:0:2301::2 dev cvd-wifiap-01") {
		t.Errorf("route to the OpenWrt LAN missing: %q", r)
	}

	f.initScript("stop")
	f.requireNoLeak(base)
}

// TestRoutedIPv6InvalidPrefix checks that a value that is not a /48 is
// rejected with a message, and that private mode is used instead.
func TestRoutedIPv6InvalidPrefix(t *testing.T) {
	for _, prefix := range []string{
		"2001:db8:cf00::/56",
		"2001:db8:cf00::/64",
		"2001:db8:cf00:1::/48",
		"2001:db8:cf00::",
		"not-a-prefix",
	} {
		t.Run(prefix, func(t *testing.T) {
			f := newIPv6Fixture(t)
			f.writeDefaults("num_cvd_accounts=1", "ipv6_routed_prefix="+prefix)
			base := f.snapshot()
			out, err := f.s.Run("sh", f.script, "start")
			if err != nil {
				t.Fatalf("init script start: %v", err)
			}
			if want := "invalid ipv6_routed_prefix '" + prefix + "'"; !strings.Contains(out.Stderr, want) {
				t.Errorf("stderr lacks %q:\n%s", want, out.Stderr)
			}

			hs := f.snapshot()
			for ifname, w := range map[string]string{
				"cvd-ebr":       "fd00:cf:24::1/64",
				"cvd-wbr":       "fd00:cf:22::1/64",
				"cvd-mtap-01":   "fd00:cf:21:1::1/64",
				"cvd-wifiap-01": "fd00:cf:23:1::1/64",
			} {
				if diff := cmp.Diff([]string{w}, globalIPv6(hs, ifname)); diff != "" {
					t.Errorf("global IPv6 of %s (-want +got):\n%s", ifname, diff)
				}
			}
			nat6 := f.sh("nft list chain ip6 cuttlefish_nat6 postrouting")
			if !strings.Contains(nat6, "ip6 saddr fd00:cf:20::/44") || strings.Count(nat6, "masquerade") != 1 {
				t.Errorf("want the private mode NAT66 rule:\n%s", nat6)
			}
			if r := f.lanRoutes(); len(r) != 0 {
				t.Errorf("OpenWrt LAN routes in private mode: %v", r)
			}

			f.initScript("stop")
			f.requireNoLeak(base)
		})
	}
}

// TestRoutedIPv6NatOverride checks ipv6_nat: 1 in routed mode NATs the
// routed /48, 0 in private mode turns NAT66 off.
func TestRoutedIPv6NatOverride(t *testing.T) {
	for _, c := range []struct {
		name     string
		defaults []string
		wantNat  string // expected masquerade source, "" for none
	}{
		{"routed_nat_on", []string{"ipv6_routed_prefix=" + routedPrefix, "ipv6_nat=1"}, routedPrefix},
		{"routed_default", []string{"ipv6_routed_prefix=" + routedPrefix}, ""},
		{"private_nat_off", []string{"ipv6_nat=0"}, ""},
		{"private_default", nil, "fd00:cf:20::/44"},
		{"invalid_value", []string{"ipv6_routed_prefix=" + routedPrefix, "ipv6_nat=yes"}, ""},
	} {
		t.Run(c.name, func(t *testing.T) {
			f := newIPv6Fixture(t)
			f.writeDefaults(append([]string{"num_cvd_accounts=1"}, c.defaults...)...)
			base := f.snapshot()
			f.initScript("start")

			nat6 := f.sh("nft list chain ip6 cuttlefish_nat6 postrouting")
			if c.wantNat == "" {
				if strings.Contains(nat6, "masquerade") {
					t.Errorf("unexpected NAT66 rule:\n%s", nat6)
				}
			} else {
				re := regexp.MustCompile(`ip6 saddr ` + regexp.QuoteMeta(c.wantNat) + ` oifname != "cvd-\*" counter packets \d+ bytes \d+ masquerade`)
				if got := len(re.FindAllString(nat6, -1)); got != 1 || strings.Count(nat6, "masquerade") != 1 {
					t.Errorf("want exactly 1 NAT66 rule for %s:\n%s", c.wantNat, nat6)
				}
			}

			f.initScript("stop")
			f.requireNoLeak(base)
		})
	}
}

// TestRoutedIPv6StartTwice checks that a second start (the ipv6-enabled
// marker is left over, the interfaces still exist) removes the routes of the
// first start before adding them again, and that stop removes them.
func TestRoutedIPv6StartTwice(t *testing.T) {
	const n = 2
	f := newIPv6Fixture(t)
	f.writeDefaults(fmt.Sprintf("num_cvd_accounts=%d", n), "ipv6_routed_prefix="+routedPrefix)

	f.initScript("start")
	f.s.Run("sh", f.script, "start") // IPv4 re-setup reports "File exists".

	if diff := cmp.Diff(wantLanRoutes(n), f.lanRoutes()); diff != "" {
		t.Errorf("OpenWrt LAN routes after second start (-want +got):\n%s", diff)
	}
	if got := strings.Count(f.sh("cat /run/cuttlefish/ipv6-routes"), "\n"); got != n {
		t.Errorf("/run/cuttlefish/ipv6-routes has %d lines, want %d", got, n)
	}

	f.initScript("stop")
	if r := f.lanRoutes(); len(r) != 0 {
		t.Errorf("OpenWrt LAN routes left after stop: %v", r)
	}
	if strings.Contains(f.sh("ls /run/cuttlefish"), "ipv6-routes") {
		t.Error("/run/cuttlefish/ipv6-routes left after stop")
	}
}

// TestRoutedIPv6ModeSwitch checks that the routes recorded by a routed mode
// start are removed by the next start even after the configuration was
// switched back to private mode (the routes file, not the configuration,
// decides what stop removes).
func TestRoutedIPv6ModeSwitch(t *testing.T) {
	f := newIPv6Fixture(t)
	f.writeDefaults("num_cvd_accounts=1", "ipv6_routed_prefix="+routedPrefix)
	f.initScript("start")
	if diff := cmp.Diff(wantLanRoutes(1), f.lanRoutes()); diff != "" {
		t.Fatalf("OpenWrt LAN routes (-want +got):\n%s", diff)
	}

	f.writeDefaults("num_cvd_accounts=1")
	f.s.Run("sh", f.script, "start") // IPv4 re-setup reports "File exists".
	if r := f.lanRoutes(); len(r) != 0 {
		t.Errorf("routed mode routes left after switching to private mode: %v", r)
	}
	hs := f.snapshot()
	for ifname, w := range map[string]string{
		"cvd-ebr":       "fd00:cf:24::1/64",
		"cvd-wbr":       "fd00:cf:22::1/64",
		"cvd-mtap-01":   "fd00:cf:21:1::1/64",
		"cvd-wifiap-01": "fd00:cf:23:1::1/64",
	} {
		if diff := cmp.Diff([]string{w}, globalIPv6(hs, ifname)); diff != "" {
			t.Errorf("global IPv6 of %s after mode switch (-want +got):\n%s", ifname, diff)
		}
	}
	nat6 := f.sh("nft list chain ip6 cuttlefish_nat6 postrouting")
	if !strings.Contains(nat6, "ip6 saddr fd00:cf:20::/44") {
		t.Errorf("want the private mode NAT66 rule after the switch:\n%s", nat6)
	}
	f.initScript("stop")
}
