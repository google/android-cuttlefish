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

// Static-mode IPv6 behaviors of cuttlefish-host-resources, checked in the
// rootless user+net+mount+pid namespace sandbox of the host_resources tests.

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/bazelbuild/rules_go/go/runfiles"
	"github.com/google/android-cuttlefish/base/cvd/host_tests/common"
	"github.com/google/go-cmp/cmp"
)

// ipv6Fixture is a sandbox with the init script, driven through
// /etc/default/cuttlefish-host-resources (a tmpfs file in the sandbox).
type ipv6Fixture struct {
	t      *testing.T
	s      *common.Sandbox
	script string
}

func newIPv6Fixture(t *testing.T) *ipv6Fixture {
	t.Helper()
	s := common.NewSandbox(t)
	t.Cleanup(s.Close)
	rel := os.Getenv("INIT_SCRIPT")
	if rel == "" {
		t.Fatal("INIT_SCRIPT env var is not set")
	}
	script, err := runfiles.Rlocation(rel)
	if err != nil {
		t.Fatalf("locating %q: %v", rel, err)
	}
	return &ipv6Fixture{t: t, s: s, script: script}
}

// sh runs a shell command in the sandbox and fails the test on error.
func (f *ipv6Fixture) sh(cmd string) string {
	f.t.Helper()
	out, err := f.s.Run("sh", "-c", cmd)
	if err != nil {
		f.t.Fatalf("%v", err)
	}
	return out.Stdout
}

// writeDefaults replaces /etc/default/cuttlefish-host-resources with lines.
// Lines must not contain single quotes.
func (f *ipv6Fixture) writeDefaults(lines ...string) {
	f.t.Helper()
	f.sh("printf '%s\\n' '" + strings.Join(lines, "' '") + "' > /etc/default/cuttlefish-host-resources")
}

// initScript runs the init script with an action (start, stop, restart).
func (f *ipv6Fixture) initScript(action string) {
	f.t.Helper()
	if _, err := f.s.Run("sh", f.script, action); err != nil {
		f.t.Fatalf("init script %s: %v", action, err)
	}
}

func (f *ipv6Fixture) setHostIPv6Disabled(disabled bool) {
	f.t.Helper()
	v := "0"
	if disabled {
		v = "1"
	}
	f.sh("echo " + v + " > /proc/sys/net/ipv6/conf/all/disable_ipv6")
}

func (f *ipv6Fixture) snapshot() common.HostState {
	f.t.Helper()
	hs, err := common.Snapshot(f.s)
	if err != nil {
		f.t.Fatalf("snapshot: %v", err)
	}
	return hs
}

// globalIPv6 returns the global IPv6 addresses ("addr/len") of ifname.
func globalIPv6(hs common.HostState, ifname string) []string {
	var out []string
	for _, a := range hs.Addrs {
		if a.Ifname != ifname {
			continue
		}
		for _, ai := range a.AddrInfo {
			if ai.Family == "inet6" && ai.Scope == "global" {
				out = append(out, fmt.Sprintf("%s/%d", ai.Local, ai.Prefixlen))
			}
		}
	}
	slices.Sort(out)
	return out
}

// raDnsmasqIfaces returns the interfaces with an RA-only dnsmasq pidfile.
func raDnsmasqIfaces(s *common.Sandbox) []string {
	var out []string
	for _, i := range common.DnsmasqPidfileIfaces(s) {
		if rest, ok := strings.CutPrefix(i, "ra-"); ok {
			out = append(out, rest)
		}
	}
	slices.Sort(out)
	return out
}

func (f *ipv6Fixture) nftTables() []string {
	var out []string
	for _, tb := range f.snapshot().Nft.Tables {
		out = append(out, tb.Family+" "+tb.Name)
	}
	slices.Sort(out)
	return out
}

// dnsmasqProcesses returns the command lines of live dnsmasq (or dnsmasq-shim)
// processes, one per line. Zombies are skipped: the sandbox's pid 1 (sleep)
// does not reap.
func (f *ipv6Fixture) dnsmasqProcesses() string {
	f.t.Helper()
	return strings.TrimSpace(f.sh(`for p in $({ pgrep -x dnsmasq; pgrep -f '[d]nsmasq-shim'; } 2>/dev/null | sort -u); do
  [ "$(awk '{print $3}' /proc/$p/stat 2>/dev/null)" = Z ] && continue
  tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null
  echo
done`))
}

// waitDnsmasq polls dnsmasqProcesses for up to 2s until ok returns true, and
// returns the last sample. Killed processes need a moment to exit.
func (f *ipv6Fixture) waitDnsmasq(ok func(string) bool) string {
	f.t.Helper()
	var out string
	for i := 0; i < 20; i++ {
		if out = f.dnsmasqProcesses(); ok(out) {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}
	return out
}

// requireNoLeak checks that stop removed everything start added: links,
// addresses, nftables, /run/cuttlefish files and dnsmasq processes.
func (f *ipv6Fixture) requireNoLeak(base common.HostState) {
	f.t.Helper()
	if diff := common.DiffState(common.Normalize(base), common.Normalize(f.snapshot())); diff != "" {
		f.t.Errorf("state leaked after stop (-before +after):\n%s", diff)
	}
	if files := common.HandleFiles(f.s); len(files) != 0 {
		f.t.Errorf("/run/cuttlefish not empty after stop: %v", files)
	}
	if p := f.waitDnsmasq(func(s string) bool { return s == "" }); p != "" {
		f.t.Errorf("dnsmasq still running after stop:\n%s", p)
	}
	if ra := raDnsmasqIfaces(f.s); len(ra) != 0 {
		f.t.Errorf("RA dnsmasq pidfiles left after stop: %v", ra)
	}
}

func tapName(kind string, i int) string { return fmt.Sprintf("cvd-%s-%02d", kind, i) }

// TestStaticIPv6Addressing checks the default ULA plan, RA placement,
// accept_ra/autoconf, and the NAT66 and RA guard rules, with 10 accounts so
// instance 10 exercises the hex prefix (fd00:cf:21:a::/64).
func TestStaticIPv6Addressing(t *testing.T) {
	const n = 10
	f := newIPv6Fixture(t)
	f.writeDefaults(fmt.Sprintf("num_cvd_accounts=%d", n))
	base := f.snapshot()
	f.initScript("start")

	hs := f.snapshot()
	want := map[string][]string{
		"cvd-ebr": {"fd00:cf:24::1/64"},
		"cvd-wbr": {"fd00:cf:22::1/64"},
	}
	for i := 1; i <= n; i++ {
		want[tapName("mtap", i)] = []string{fmt.Sprintf("fd00:cf:21:%x::1/64", i)}
		want[tapName("wifiap", i)] = []string{fmt.Sprintf("fd00:cf:23:%x::1/64", i)}
		want[tapName("etap", i)] = nil
		want[tapName("wtap", i)] = nil
	}
	for ifname, w := range want {
		if diff := cmp.Diff(w, globalIPv6(hs, ifname)); diff != "" {
			t.Errorf("global IPv6 of %s (-want +got):\n%s", ifname, diff)
		}
	}

	// RAs on the bridges and on every cvd-wifiap-XX, never on cvd-mtap-XX.
	wantRA := []string{"cvd-ebr", "cvd-wbr"}
	for i := 1; i <= n; i++ {
		wantRA = append(wantRA, tapName("wifiap", i))
	}
	slices.Sort(wantRA)
	if diff := cmp.Diff(wantRA, raDnsmasqIfaces(f.s)); diff != "" {
		t.Errorf("RA dnsmasq interfaces (-want +got):\n%s", diff)
	}

	// The RA dnsmasq advertises the interface's prefix, ra-only (no DHCPv6).
	for ifname, prefix := range map[string]string{
		"cvd-ebr":            "fd00:cf:24::",
		"cvd-wbr":            "fd00:cf:22::",
		tapName("wifiap", n): fmt.Sprintf("fd00:cf:23:%x::", n),
	} {
		cmdline := f.sh(fmt.Sprintf("tr '\\0' ' ' < /proc/$(cat /run/cuttlefish-dnsmasq-ra-%s.pid)/cmdline", ifname))
		for _, arg := range []string{"--enable-ra", "--interface=" + ifname, "--dhcp-range=" + prefix + ",ra-only,64"} {
			if !strings.Contains(cmdline, arg) {
				t.Errorf("RA dnsmasq on %s lacks %q: %s", ifname, arg, cmdline)
			}
		}
	}

	// The host ignores RAs from guests on every cuttlefish interface.
	ifaces := []string{"cvd-ebr", "cvd-wbr"}
	for i := 1; i <= n; i++ {
		ifaces = append(ifaces, tapName("etap", i), tapName("wtap", i), tapName("mtap", i), tapName("wifiap", i))
	}
	for _, ifname := range ifaces {
		for _, key := range []string{"accept_ra", "autoconf"} {
			if v := strings.TrimSpace(f.sh(fmt.Sprintf("cat /proc/sys/net/ipv6/conf/%s/%s", ifname, key))); v != "0" {
				t.Errorf("%s/%s = %s, want 0", ifname, key, v)
			}
		}
	}

	nat6 := f.sh("nft list chain ip6 cuttlefish_nat6 postrouting")
	natRule := regexp.MustCompile(`ip6 saddr fd00:cf:20::/44 oifname != "cvd-\*" counter packets \d+ bytes \d+ masquerade`)
	if got := len(natRule.FindAllString(nat6, -1)); got != 1 {
		t.Errorf("want exactly 1 NAT66 rule excluding cvd-* egress, got %d:\n%s", got, nat6)
	}

	bridgeGuard := f.sh("nft list chain bridge cuttlefish_ra_guard prerouting")
	inetGuard := f.sh("nft list chain inet cuttlefish_ra_guard input")
	for _, c := range []struct{ out, pattern string }{
		{bridgeGuard, "cvd-etap-*"},
		{bridgeGuard, "cvd-wtap-*"},
		{inetGuard, "cvd-mtap-*"},
		{inetGuard, "cvd-wifiap-*"},
	} {
		if !guardRuleFor(c.out, c.pattern) {
			t.Errorf("no RA/redirect drop rule for %s:\n%s", c.pattern, c.out)
		}
	}
	if strings.Contains(bridgeGuard+inetGuard, "nd-router-solicit") || strings.Contains(bridgeGuard+inetGuard, "nd-neighbor") {
		t.Errorf("RA guard must not drop RS/NS/NA:\n%s\n%s", bridgeGuard, inetGuard)
	}

	f.initScript("stop")
	f.requireNoLeak(base)
}

// guardRuleFor reports whether out has a rule dropping RAs and redirects
// arriving on ifname pattern.
func guardRuleFor(out, pattern string) bool {
	for _, line := range strings.Split(out, "\n") {
		if strings.Contains(line, `iifname "`+pattern+`"`) &&
			strings.Contains(line, "nd-router-advert") &&
			strings.Contains(line, "nd-redirect") &&
			strings.HasSuffix(strings.TrimSpace(line), "drop") {
			return true
		}
	}
	return false
}

// sendICMPv6 sends 3 ICMPv6 messages of type icmpType (134 = RA, 133 = RS)
// out of ifname to the all-nodes / all-routers group, with hop limit 255.
const sendICMPv6 = `
import socket, struct, sys
ifname, icmp_type = sys.argv[1], int(sys.argv[2])
s = socket.socket(socket.AF_INET6, socket.SOCK_RAW, socket.IPPROTO_ICMPV6)
s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 255)
s.setsockopt(socket.SOL_SOCKET, 25, ifname.encode())  # SO_BINDTODEVICE
if icmp_type == 134:
    msg, dst = struct.pack("!BBHBBHII", 134, 0, 0, 64, 0, 1800, 0, 0), "ff02::1"
else:
    msg, dst = struct.pack("!BBHI", 133, 0, 0, 0), "ff02::2"
for _ in range(3):
    s.sendto(msg, (dst, 0, 0, socket.if_nametoindex(ifname)))
`

// guardCounter returns the packet counter of the RA guard rule for pattern.
func guardCounter(t *testing.T, out, pattern string) int {
	t.Helper()
	re := regexp.MustCompile(`counter packets (\d+)`)
	for _, line := range strings.Split(out, "\n") {
		if strings.Contains(line, `iifname "`+pattern+`"`) {
			if m := re.FindStringSubmatch(line); m != nil {
				v, _ := strconv.Atoi(m[1])
				return v
			}
		}
	}
	t.Fatalf("no counter for %s in:\n%s", pattern, out)
	return 0
}

// TestStaticIPv6RAGuardDropsGuestRA sends RAs and RSs from a fake guest on a
// bridged tap and on a routed tap, and checks that the RA guard drops RAs
// only.
func TestStaticIPv6RAGuardDropsGuestRA(t *testing.T) {
	f := newIPv6Fixture(t)
	if _, err := f.s.Run("python3", "-c", "import socket"); err != nil {
		t.Skipf("python3 is required to craft ICMPv6: %v", err)
	}
	f.writeDefaults("num_cvd_accounts=1")
	f.initScript("start")
	t.Cleanup(func() { f.s.Run("sh", f.script, "stop") })

	// Fake guests: veth peers named like cuttlefish taps. The guest side has
	// no DAD so its link-local source address is usable immediately.
	f.sh(`set -e
ip link add cvd-etap-99 type veth peer name guest-e
ip link set cvd-etap-99 master cvd-ebr up
ip link add cvd-mtap-99 type veth peer name guest-m
ip link set cvd-mtap-99 up
for g in guest-e guest-m; do
  echo 0 > /proc/sys/net/ipv6/conf/$g/accept_dad
  ip link set $g up
done`)

	cases := []struct {
		guest, family, chain, pattern string
	}{
		{"guest-e", "bridge", "prerouting", "cvd-etap-*"},
		{"guest-m", "inet", "input", "cvd-mtap-*"},
	}
	for _, c := range cases {
		t.Run(c.pattern, func(t *testing.T) {
			list := "nft list chain " + c.family + " cuttlefish_ra_guard " + c.chain
			before := guardCounter(t, f.sh(list), c.pattern)
			if _, err := f.s.Run("python3", "-c", sendICMPv6, c.guest, "133"); err != nil {
				t.Fatalf("sending RS: %v", err)
			}
			if got := guardCounter(t, f.sh(list), c.pattern); got != before {
				t.Errorf("RS was dropped: counter %d -> %d", before, got)
			}
			if _, err := f.s.Run("python3", "-c", sendICMPv6, c.guest, "134"); err != nil {
				t.Fatalf("sending RA: %v", err)
			}
			if got := guardCounter(t, f.sh(list), c.pattern); got != before+3 {
				t.Errorf("RA guard counter %d -> %d, want +3", before, got)
			}
		})
	}
	f.sh("ip link del cvd-etap-99; ip link del cvd-mtap-99")
}

// TestStaticIPv6HostDisabled checks the path where the host has IPv6
// disabled: IPv4 is set up as usual, and nothing IPv6 is added.
func TestStaticIPv6HostDisabled(t *testing.T) {
	f := newIPv6Fixture(t)
	f.writeDefaults("num_cvd_accounts=2")
	f.setHostIPv6Disabled(true)
	base := f.snapshot()
	f.initScript("start")

	hs := f.snapshot()
	if got := hs.PrimaryIPv4("cvd-ebr"); got != "192.168.98.1/24" {
		t.Errorf("cvd-ebr IPv4 = %q, want 192.168.98.1/24", got)
	}
	if got := hs.PrimaryIPv4("cvd-mtap-02"); got != "192.168.97.5/30" {
		t.Errorf("cvd-mtap-02 IPv4 = %q, want 192.168.97.5/30", got)
	}
	wantTables := []string{"bridge cuttlefish_bridge", "ip cuttlefish_nat"}
	if diff := cmp.Diff(wantTables, f.nftTables()); diff != "" {
		t.Errorf("nft tables with IPv6 disabled (-want +got):\n%s", diff)
	}
	for _, l := range hs.Links {
		if g := globalIPv6(hs, l.Ifname); len(g) != 0 {
			t.Errorf("%s has IPv6 %v with host IPv6 disabled", l.Ifname, g)
		}
	}
	if ra := raDnsmasqIfaces(f.s); len(ra) != 0 {
		t.Errorf("RA dnsmasq started with host IPv6 disabled: %v", ra)
	}
	if slices.Contains(common.HandleFiles(f.s), "ipv6-enabled") {
		t.Error("ipv6-enabled marker created with host IPv6 disabled")
	}

	f.initScript("stop")
	f.requireNoLeak(base)
}

// TestStaticIPv6DisabledBeforeStop checks that stop removes the IPv6 state
// that start created even when the host disabled IPv6 in between.
func TestStaticIPv6DisabledBeforeStop(t *testing.T) {
	f := newIPv6Fixture(t)
	f.writeDefaults("num_cvd_accounts=1")
	f.initScript("start")
	f.setHostIPv6Disabled(true)
	f.initScript("stop")

	if tables := f.nftTables(); len(tables) != 0 {
		t.Errorf("nft tables left after stop: %v", tables)
	}
	if files := common.HandleFiles(f.s); len(files) != 0 {
		t.Errorf("/run/cuttlefish not empty after stop: %v", files)
	}
	if p := f.waitDnsmasq(func(s string) bool { return s == "" }); p != "" {
		t.Errorf("dnsmasq still running after stop:\n%s", p)
	}
}

// TestStaticIPv6DefaultOverrides checks that the IPv6 settings of
// /etc/default/cuttlefish-host-resources are applied.
func TestStaticIPv6DefaultOverrides(t *testing.T) {
	f := newIPv6Fixture(t)
	f.writeDefaults(
		"num_cvd_accounts=1",
		"ethernet_ipv6_prefix=fd00:cf:2a::",
		"ethernet_ipv6_prefix_length=64",
		"wifi_ipv6_prefix=fd00:cf:2b::",
		"wifi_ipv6_prefix_length=64",
		"mobile_ipv6_prefix_base=fd00:cf:2c",
		"wifiap_ipv6_prefix_base=fd00:cf:2d",
		"dns6_servers=fd00:cf:2e::53",
	)
	base := f.snapshot()
	f.initScript("start")

	hs := f.snapshot()
	for ifname, w := range map[string]string{
		"cvd-ebr":       "fd00:cf:2a::1/64",
		"cvd-wbr":       "fd00:cf:2b::1/64",
		"cvd-mtap-01":   "fd00:cf:2c:1::1/64",
		"cvd-wifiap-01": "fd00:cf:2d:1::1/64",
	} {
		if diff := cmp.Diff([]string{w}, globalIPv6(hs, ifname)); diff != "" {
			t.Errorf("global IPv6 of %s (-want +got):\n%s", ifname, diff)
		}
	}
	cmdline := f.sh("tr '\\0' ' ' < /proc/$(cat /run/cuttlefish-dnsmasq-ra-cvd-ebr.pid)/cmdline")
	for _, arg := range []string{"--dhcp-range=fd00:cf:2a::,ra-only,64", "option6:dns-server,fd00:cf:2e::53"} {
		if !strings.Contains(cmdline, arg) {
			t.Errorf("RA dnsmasq on cvd-ebr lacks %q: %s", arg, cmdline)
		}
	}

	f.initScript("stop")
	f.requireNoLeak(base)
}

// TestStaticIPv6Restart checks that start/stop cycles and the restart action
// leave exactly one copy of each IPv6 rule and clean up completely.
func TestStaticIPv6Restart(t *testing.T) {
	f := newIPv6Fixture(t)
	f.writeDefaults("num_cvd_accounts=2")
	base := f.snapshot()

	f.initScript("start")
	f.initScript("stop")
	f.initScript("start")
	f.initScript("restart")

	nat6 := f.sh("nft list chain ip6 cuttlefish_nat6 postrouting")
	if got := strings.Count(nat6, "masquerade"); got != 1 {
		t.Errorf("want 1 NAT66 rule after restart, got %d:\n%s", got, nat6)
	}
	inetGuard := f.sh("nft list chain inet cuttlefish_ra_guard input")
	if got := strings.Count(inetGuard, "drop"); got != 2 {
		t.Errorf("want 2 inet RA guard rules after restart, got %d:\n%s", got, inetGuard)
	}
	hs := f.snapshot()
	if diff := cmp.Diff([]string{"fd00:cf:21:2::1/64"}, globalIPv6(hs, "cvd-mtap-02")); diff != "" {
		t.Errorf("cvd-mtap-02 IPv6 after restart (-want +got):\n%s", diff)
	}
	// One RA dnsmasq per interface: the restarted ones replaced the old ones.
	const wantRA = 2 + 2 // cvd-ebr, cvd-wbr, cvd-wifiap-01, cvd-wifiap-02
	countRA := func(s string) int { return strings.Count(s, "--enable-ra") }
	if procs := f.waitDnsmasq(func(s string) bool { return countRA(s) == wantRA }); countRA(procs) != wantRA {
		t.Errorf("%d RA dnsmasq processes after restart, want %d:\n%s", countRA(procs), wantRA, procs)
	}

	f.initScript("stop")
	f.requireNoLeak(base)
}

// TestStaticIPv6StartTwice checks that start without a stop in between (the
// ipv6-enabled marker is left over) replaces the IPv6 setup instead of
// duplicating it: one RA dnsmasq per interface, one copy of each nft rule,
// and a following stop removes everything IPv6. IPv4 is not checked here:
// its setup is not idempotent (duplicate masquerade rules and dnsmasq).
func TestStaticIPv6StartTwice(t *testing.T) {
	f := newIPv6Fixture(t)
	f.writeDefaults("num_cvd_accounts=2")

	f.initScript("start")
	f.s.Run("sh", f.script, "start") // IPv4 re-setup reports "File exists".

	for chain, want := range map[string]int{
		"ip6 cuttlefish_nat6 postrouting":       1,
		"ip6 filter FORWARD":                    2,
		"bridge cuttlefish_ra_guard prerouting": 2,
		"inet cuttlefish_ra_guard input":        2,
	} {
		out := f.sh("nft list chain " + chain)
		if got := strings.Count(out, "counter packets"); got != want {
			t.Errorf("%s: unexpected rules after second start, got %d want %d:\n%s", chain, got, want, out)
		}
	}
	const wantRA = 2 + 2 // cvd-ebr, cvd-wbr, cvd-wifiap-01, cvd-wifiap-02
	countRA := func(s string) int { return strings.Count(s, "--enable-ra") }
	if procs := f.waitDnsmasq(func(s string) bool { return countRA(s) == wantRA }); countRA(procs) != wantRA {
		t.Errorf("%d RA dnsmasq processes after second start, want %d:\n%s", countRA(procs), wantRA, procs)
	}

	f.initScript("stop")
	if procs := f.waitDnsmasq(func(s string) bool { return countRA(s) == 0 }); countRA(procs) != 0 {
		t.Errorf("RA dnsmasq still running after stop:\n%s", procs)
	}
	if ra := raDnsmasqIfaces(f.s); len(ra) != 0 {
		t.Errorf("RA dnsmasq pidfiles left after stop: %v", ra)
	}
	for _, tb := range f.nftTables() {
		if strings.Contains(tb, "nat6") || strings.Contains(tb, "ra_guard") || strings.Contains(tb, "ip6 filter") {
			t.Errorf("IPv6 nft table left after stop: %s", tb)
		}
	}
}

// TestStaticIPv6UnmanagedBridge checks bridge_interface: the host does not
// address or advertise on a bridge it does not manage, but still sets up the
// routed taps.
func TestStaticIPv6UnmanagedBridge(t *testing.T) {
	f := newIPv6Fixture(t)
	f.sh("ip link add br-test type bridge && ip link set br-test up")
	f.writeDefaults("num_cvd_accounts=1", "bridge_interface=br-test")
	base := f.snapshot()
	f.initScript("start")

	hs := f.snapshot()
	if g := globalIPv6(hs, "br-test"); len(g) != 0 {
		t.Errorf("unmanaged bridge got IPv6 %v", g)
	}
	if diff := cmp.Diff([]string{"cvd-wifiap-01"}, raDnsmasqIfaces(f.s)); diff != "" {
		t.Errorf("RA dnsmasq interfaces (-want +got):\n%s", diff)
	}
	if diff := cmp.Diff([]string{"fd00:cf:21:1::1/64"}, globalIPv6(hs, "cvd-mtap-01")); diff != "" {
		t.Errorf("cvd-mtap-01 IPv6 (-want +got):\n%s", diff)
	}

	f.initScript("stop")
	f.requireNoLeak(base)
}

// TestStaticIPv6PreservesUpstreamRA checks that enabling
// net.ipv6.conf.all.forwarding=1 promotes accept_ra from 1 to 2 on default and
// on existing upstream interfaces (or interfaces with RTF_ADDRCONF default
// routes) so kernel RA default routes are not purged by rt6_purge_dflt_routers
// and future RAs continue to be accepted, while interfaces with accept_ra=0
// (e.g. NetworkManager userspace RA or cvd-*) stay at 0.
func TestStaticIPv6PreservesUpstreamRA(t *testing.T) {
	f := newIPv6Fixture(t)
	if _, err := f.s.Run("python3", "-c", "import socket"); err != nil {
		t.Skipf("python3 is required to craft ICMPv6: %v", err)
	}
	f.writeDefaults("num_cvd_accounts=1")

	// Simulate an upstream interface eth0 (accept_ra=1) that received an RA
	// from upstream-rtr, plus a dhclient0 interface that received an RA and
	// then had accept_ra reset to 0 by dhclient-script, plus an nm0 interface
	// with accept_ra=0 and no kernel RA route.
	f.sh(`set -e
echo 0 > /proc/sys/net/ipv6/conf/all/forwarding
ip link add eth0 type veth peer name upstream-rtr
ip link add dhclient0 type veth peer name dhclient-rtr
ip link add nm0 type dummy
for dev in eth0 upstream-rtr dhclient0 dhclient-rtr nm0; do
  echo 0 > /proc/sys/net/ipv6/conf/$dev/accept_dad
done
echo 1 > /proc/sys/net/ipv6/conf/eth0/accept_ra
echo 1 > /proc/sys/net/ipv6/conf/dhclient0/accept_ra
echo 0 > /proc/sys/net/ipv6/conf/nm0/accept_ra
ip link set eth0 up
ip link set upstream-rtr up
ip link set dhclient0 up
ip link set dhclient-rtr up
ip link set nm0 up`)

	if _, err := f.s.Run("python3", "-c", sendICMPv6, "upstream-rtr", "134"); err != nil {
		t.Fatalf("sending RA on upstream-rtr: %v", err)
	}
	if _, err := f.s.Run("python3", "-c", sendICMPv6, "dhclient-rtr", "134"); err != nil {
		t.Fatalf("sending RA on dhclient-rtr: %v", err)
	}
	// Simulate Debian 12 dhclient-script resetting accept_ra=0 after kernel RA route installation.
	f.sh("echo 0 > /proc/sys/net/ipv6/conf/dhclient0/accept_ra")

	routesBefore := f.sh("ip -6 route show default")
	if !strings.Contains(routesBefore, "dev eth0 proto ra") || !strings.Contains(routesBefore, "dev dhclient0 proto ra") {
		t.Fatalf("expected proto ra default routes on eth0 and dhclient0 before start, got:\n%s", routesBefore)
	}

	f.initScript("start")
	t.Cleanup(func() { f.s.Run("sh", f.script, "stop") })

	for _, dev := range []string{"default", "eth0", "dhclient0"} {
		if got := strings.TrimSpace(f.sh("cat /proc/sys/net/ipv6/conf/" + dev + "/accept_ra")); got != "2" {
			t.Errorf("%s/accept_ra = %s after start, want 2", dev, got)
		}
	}
	if got := strings.TrimSpace(f.sh("cat /proc/sys/net/ipv6/conf/nm0/accept_ra")); got != "0" {
		t.Errorf("nm0/accept_ra = %s after start, want 0", got)
	}

	routesAfter := f.sh("ip -6 route show default")
	if !strings.Contains(routesAfter, "dev eth0 proto ra") || !strings.Contains(routesAfter, "dev dhclient0 proto ra") {
		t.Errorf("RA default routes were purged by start:\n%s", routesAfter)
	}

	// Flush eth0's default route and send another RA while forwarding=1 to
	// verify future RAs are still accepted on eth0.
	f.sh("ip -6 route flush default dev eth0")
	if _, err := f.s.Run("python3", "-c", sendICMPv6, "upstream-rtr", "134"); err != nil {
		t.Fatalf("sending second RA on upstream-rtr: %v", err)
	}
	if routes := f.sh("ip -6 route show default dev eth0"); !strings.Contains(routes, "proto ra") {
		t.Errorf("eth0 did not accept RA with forwarding=1: %q", routes)
	}
}

// TestStaticIPv6ForwardAcceptWithDockerDrop checks that explicit
// iifname/oifname "cvd-*" accept rules in ip6 filter FORWARD allow routed
// Cuttlefish IPv6 traffic in both directions (outbound from cvd-* and inbound
// to cvd-*) while dropping non-cvd-* traffic when Docker sets
// ip6tables -P FORWARD DROP (both when Docker starts before Cuttlefish and
// when Docker starts after Cuttlefish), remain compatible with ip6tables-nft,
// and are cleanly removed on stop without disturbing Docker's chains or policy.
func TestStaticIPv6ForwardAcceptWithDockerDrop(t *testing.T) {
	for _, dockerFirst := range []bool{true, false} {
		name := "cuttlefish_before_docker"
		if dockerFirst {
			name = "docker_before_cuttlefish"
		}
		t.Run(name, func(t *testing.T) {
			f := newIPv6Fixture(t)
			f.writeDefaults("num_cvd_accounts=1")

			setupDocker := func() {
				f.sh(`set -e
ip6tables -P FORWARD DROP
ip6tables -N DOCKER-USER
ip6tables -A DOCKER-USER -j RETURN
ip6tables -I FORWARD 1 -j DOCKER-USER`)
			}

			if dockerFirst {
				setupDocker()
				f.initScript("start")
			} else {
				f.initScript("start")
				setupDocker()
			}

			rules := f.sh("ip6tables -S FORWARD")
			for _, want := range []string{
				"-P FORWARD DROP",
				"-A FORWARD -j DOCKER-USER",
				"-A FORWARD -i cvd-+ -j ACCEPT",
				"-A FORWARD -o cvd-+ -j ACCEPT",
			} {
				if !strings.Contains(rules, want) {
					t.Errorf("ip6tables -S FORWARD lacks %q:\n%s", want, rules)
				}
			}

			// Send:
			// 1. Outbound packet from guest-m -> cvd-mtap-99 destined to 2001:db8:1::1 (via eth0), matching iifname "cvd-*".
			// 2. Inbound packet from wan0 -> eth0 destined to fd00:cf:21:99::2 (via cvd-mtap-99), matching oifname "cvd-*".
			// 3. Non-Cuttlefish packet from other-peer -> other0 destined to 2001:db8:1::1 (via eth0), which must be dropped by policy DROP.
			f.sh(`set -e
ip link add cvd-mtap-99 address 02:00:00:00:00:01 type veth peer name guest-m address 02:00:00:00:00:02
ip link add eth0 address 02:00:00:00:00:11 type veth peer name wan0 address 02:00:00:00:00:12
ip link add other0 address 02:00:00:00:00:21 type veth peer name other-peer address 02:00:00:00:00:22
for d in cvd-mtap-99 guest-m eth0 wan0 other0 other-peer; do
  echo 0 > /proc/sys/net/ipv6/conf/$d/accept_dad
  ip link set $d up
done
echo 0 > /proc/sys/net/ipv6/conf/guest-m/forwarding
echo 0 > /proc/sys/net/ipv6/conf/wan0/forwarding
ip -6 addr add fd00:cf:21:99::1/64 dev cvd-mtap-99
ip -6 neigh add fd00:cf:21:99::2 lladdr 02:00:00:00:00:02 dev cvd-mtap-99
ip -6 addr add 2001:db8:1::2/64 dev eth0
ip -6 neigh add 2001:db8:1::1 lladdr 02:00:00:00:00:12 dev eth0
ip -6 addr add fd00:99::1/64 dev other0
python3 -c '
import socket, struct
def send_pkt(tx_if, dst_mac, src_mac, src_ip_str, dst_ip_str):
    eth = dst_mac + src_mac + b"\x86\xdd"
    src_ip = socket.inet_pton(socket.AF_INET6, src_ip_str)
    dst_ip = socket.inet_pton(socket.AF_INET6, dst_ip_str)
    icmp6 = struct.pack("!BBHHH", 128, 0, 0, 1, 1)
    ip6 = struct.pack("!IHBB", (6 << 28), len(icmp6), 58, 64) + src_ip + dst_ip
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
    s.bind((tx_if, 0))
    s.send(eth + ip6 + icmp6)
    s.close()

send_pkt("guest-m", b"\x02\x00\x00\x00\x00\x01", b"\x02\x00\x00\x00\x00\x02", "fd00:cf:21:99::2", "2001:db8:1::1")
send_pkt("wan0", b"\x02\x00\x00\x00\x00\x11", b"\x02\x00\x00\x00\x00\x12", "2001:db8:1::1", "fd00:cf:21:99::2")
send_pkt("other-peer", b"\x02\x00\x00\x00\x00\x21", b"\x02\x00\x00\x00\x00\x22", "fd00:99::2", "2001:db8:1::1")
'
ip link del cvd-mtap-99
ip link del eth0
ip link del other0`)

			fwdChain := f.sh("nft list chain ip6 filter FORWARD")
			if !regexp.MustCompile(`iifname "cvd-\*" counter packets [1-9]\d* bytes [1-9]\d* accept`).MatchString(fwdChain) {
				t.Errorf("expected iifname cvd-* accept counter > 0 in ip6 filter FORWARD:\n%s", fwdChain)
			}
			if !regexp.MustCompile(`oifname "cvd-\*" counter packets [1-9]\d* bytes [1-9]\d* accept`).MatchString(fwdChain) {
				t.Errorf("expected oifname cvd-* accept counter > 0 in ip6 filter FORWARD:\n%s", fwdChain)
			}
			iptVerbose := f.sh("ip6tables -v -L FORWARD")
			if !regexp.MustCompile(`policy DROP [1-9]\d* packets`).MatchString(iptVerbose) {
				t.Errorf("expected non-cvd-* packet to be dropped by FORWARD policy DROP:\n%s", iptVerbose)
			}

			f.initScript("stop")
			afterStop := f.sh("ip6tables -S FORWARD")
			if !strings.Contains(afterStop, "-P FORWARD DROP") || !strings.Contains(afterStop, "-A FORWARD -j DOCKER-USER") {
				t.Errorf("Docker FORWARD state lost after stop:\n%s", afterStop)
			}
			if strings.Contains(afterStop, "cvd-") {
				t.Errorf("cvd-* FORWARD rule leaked after stop:\n%s", afterStop)
			}
		})
	}
}

// TestStaticIPv6ForwardingReadOnly checks read-only sysctl handling for both
// IPv6 and IPv4 forwarding, as well as the packaged sysctl.d configuration:
//   - when /proc/sys/net/ipv6/conf/all/forwarding is read-only and 0, start logs
//     an explicit error to stderr and skips IPv6 setup while keeping IPv4 working;
//   - when it is read-only and already 1 (as in podcvd with --sysctl), start
//     succeeds and configures IPv6 normally;
//   - when /proc/sys/net/ipv4/ip_forward is read-only and 0, start fails with an
//     explicit error when allocate_static_resources=1, but succeeds without
//     mutating sysctls (and still sets up /.dockerenv device permissions) when
//     allocate_static_resources=0;
//   - 90-cuttlefish-ip-forward.conf sets net.ipv4.ip_forward=1 and
//     net.ipv6.conf.default.accept_ra=2 without enabling early-boot
//     net.ipv6.conf.all.forwarding=1.
func TestStaticIPv6ForwardingReadOnly(t *testing.T) {
	t.Run("read_only_zero_logs_and_skips_ipv6", func(t *testing.T) {
		f := newIPv6Fixture(t)
		f.writeDefaults("num_cvd_accounts=1")
		f.sh(`set -e
echo 0 > /proc/sys/net/ipv6/conf/all/forwarding
echo 0 > /tmp/ro_zero
mount --bind /tmp/ro_zero /proc/sys/net/ipv6/conf/all/forwarding
mount -o remount,bind,ro /proc/sys/net/ipv6/conf/all/forwarding`)
		t.Cleanup(func() { f.s.Run("umount", "/proc/sys/net/ipv6/conf/all/forwarding") })

		out, err := f.s.Run("sh", f.script, "start")
		if err != nil {
			t.Fatalf("start failed: %v", err)
		}
		if want := "failed to enable net.ipv6.conf.all.forwarding; skipping IPv6 setup"; !strings.Contains(out.Stderr, want) {
			t.Errorf("stderr lacks %q:\n%s", want, out.Stderr)
		}
		hs := f.snapshot()
		if got := hs.PrimaryIPv4("cvd-ebr"); got != "192.168.98.1/24" {
			t.Errorf("cvd-ebr IPv4 = %q, want 192.168.98.1/24", got)
		}
		if g := globalIPv6(hs, "cvd-ebr"); len(g) != 0 {
			t.Errorf("cvd-ebr got IPv6 %v when forwarding could not be enabled", g)
		}
		f.initScript("stop")
	})

	t.Run("read_only_preset_one_succeeds", func(t *testing.T) {
		f := newIPv6Fixture(t)
		f.writeDefaults("num_cvd_accounts=1")
		f.sh(`set -e
echo 1 > /proc/sys/net/ipv6/conf/all/forwarding
echo 1 > /tmp/ro_one
mount --bind /tmp/ro_one /proc/sys/net/ipv6/conf/all/forwarding
mount -o remount,bind,ro /proc/sys/net/ipv6/conf/all/forwarding`)
		t.Cleanup(func() { f.s.Run("umount", "/proc/sys/net/ipv6/conf/all/forwarding") })

		f.initScript("start")
		hs := f.snapshot()
		if diff := cmp.Diff([]string{"fd00:cf:21:1::1/64"}, globalIPv6(hs, "cvd-mtap-01")); diff != "" {
			t.Errorf("cvd-mtap-01 IPv6 (-want +got):\n%s", diff)
		}
		f.initScript("stop")
	})

	t.Run("ipv4_read_only_zero_fails_when_static_allocated", func(t *testing.T) {
		f := newIPv6Fixture(t)
		f.writeDefaults("num_cvd_accounts=1", "allocate_static_resources=1")
		f.sh(`set -e
echo 0 > /proc/sys/net/ipv4/ip_forward
echo 0 > /tmp/ro_v4_zero
mount --bind /tmp/ro_v4_zero /proc/sys/net/ipv4/ip_forward
mount -o remount,bind,ro /proc/sys/net/ipv4/ip_forward`)
		t.Cleanup(func() { f.s.Run("umount", "/proc/sys/net/ipv4/ip_forward") })

		out, err := f.s.Run("sh", f.script, "start")
		if err == nil {
			t.Fatalf("expected start to fail when ip_forward is read-only 0 and allocate_static_resources=1")
		}
		if want := "failed to enable net.ipv4.ip_forward"; !strings.Contains(out.Stderr, want) {
			t.Errorf("stderr lacks %q:\n%s", want, out.Stderr)
		}
	})

	t.Run("ipv4_read_only_zero_succeeds_when_cvdalloc", func(t *testing.T) {
		f := newIPv6Fixture(t)
		f.writeDefaults("allocate_static_resources=0")
		f.sh(`set -e
echo 0 > /proc/sys/net/ipv4/ip_forward
echo 0 > /proc/sys/net/ipv6/conf/all/forwarding
echo 0 > /tmp/ro_v4_zero
mount --bind /tmp/ro_v4_zero /proc/sys/net/ipv4/ip_forward
mount -o remount,bind,ro /proc/sys/net/ipv4/ip_forward`)
		t.Cleanup(func() { f.s.Run("umount", "/proc/sys/net/ipv4/ip_forward") })

		f.initScript("start")
		if got := strings.TrimSpace(f.sh("cat /proc/sys/net/ipv6/conf/all/forwarding")); got != "0" {
			t.Errorf("all/forwarding mutated to %s when allocate_static_resources=0, want 0", got)
		}
		f.initScript("stop")
	})

	t.Run("sysctl_d_conf_sets_default_accept_ra_2", func(t *testing.T) {
		f := newIPv6Fixture(t)
		realInit, err := filepath.EvalSymlinks(f.script)
		if err != nil {
			t.Fatalf("EvalSymlinks(%q): %v", f.script, err)
		}
		sysctlConf := filepath.Join(filepath.Dir(realInit), "../host/packages/cuttlefish-base/usr/lib/sysctl.d/90-cuttlefish-ip-forward.conf")
		f.sh(fmt.Sprintf(`set -e
echo 0 > /proc/sys/net/ipv4/ip_forward
echo 1 > /proc/sys/net/ipv6/conf/default/accept_ra
echo 0 > /proc/sys/net/ipv6/conf/all/forwarding
sysctl -p %q`, sysctlConf))
		if got := strings.TrimSpace(f.sh("cat /proc/sys/net/ipv4/ip_forward")); got != "1" {
			t.Errorf("net.ipv4.ip_forward = %s, want 1", got)
		}
		if got := strings.TrimSpace(f.sh("cat /proc/sys/net/ipv6/conf/default/accept_ra")); got != "2" {
			t.Errorf("net.ipv6.conf.default.accept_ra = %s, want 2", got)
		}
		if got := strings.TrimSpace(f.sh("cat /proc/sys/net/ipv6/conf/all/forwarding")); got != "0" {
			t.Errorf("net.ipv6.conf.all.forwarding = %s after loading sysctl.d conf, want 0", got)
		}
	})
}

// TestStaticIPv6DefaultDisableIPv6 checks that when a host sets
// net.ipv6.conf.default.disable_ipv6=1 while net.ipv6.conf.all.disable_ipv6=0,
// start_ipv6 enables IPv6 on the cvd-* bridges and taps and configures IPv6
// addresses without error.
func TestStaticIPv6DefaultDisableIPv6(t *testing.T) {
	f := newIPv6Fixture(t)
	f.writeDefaults("num_cvd_accounts=1")
	f.sh("echo 1 > /proc/sys/net/ipv6/conf/default/disable_ipv6")
	base := f.snapshot()
	f.initScript("start")

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

	f.initScript("stop")
	f.requireNoLeak(base)
}

// TestStaticIPv6ShrinkAccountsStopsOrphanRaDnsmasq checks that when
// num_cvd_accounts is reduced in /etc/default/cuttlefish-host-resources
// between start and stop, stop_ipv6 terminates all RA dnsmasq daemons via
// /var/run/cuttlefish-dnsmasq-ra-*.pid rather than leaving higher-numbered
// instances running.
func TestStaticIPv6ShrinkAccountsStopsOrphanRaDnsmasq(t *testing.T) {
	f := newIPv6Fixture(t)
	f.writeDefaults("num_cvd_accounts=2")
	f.initScript("start")

	if diff := cmp.Diff([]string{"cvd-ebr", "cvd-wbr", "cvd-wifiap-01", "cvd-wifiap-02"}, raDnsmasqIfaces(f.s)); diff != "" {
		t.Fatalf("RA dnsmasq interfaces after start (-want +got):\n%s", diff)
	}

	// Shrink num_cvd_accounts from 2 to 1 before stopping.
	f.writeDefaults("num_cvd_accounts=1")
	f.initScript("stop")

	if ra := raDnsmasqIfaces(f.s); len(ra) != 0 {
		t.Errorf("RA dnsmasq pidfiles left after stop with shrunk num_cvd_accounts: %v", ra)
	}
	if p := f.waitDnsmasq(func(s string) bool { return s == "" }); p != "" {
		t.Errorf("dnsmasq still running after stop with shrunk num_cvd_accounts:\n%s", p)
	}
}

// TestStaticIPv6PostrmPurgeCleanup checks that running
// cuttlefish-base.postrm purge removes routed IPv6 prefixes, cuttlefish_*
// nftables tables, ip6 filter FORWARD rules, dnsmasq processes, and
// /run/cuttlefish state even if stop was not run prior to purge.
func TestStaticIPv6PostrmPurgeCleanup(t *testing.T) {
	f := newIPv6Fixture(t)
	postrmRel := os.Getenv("POSTRM_SCRIPT")
	if postrmRel == "" {
		t.Fatal("POSTRM_SCRIPT env var is not set")
	}
	postrm, err := runfiles.Rlocation(postrmRel)
	if err != nil {
		t.Fatalf("locating %q: %v", postrmRel, err)
	}
	f.writeDefaults("num_cvd_accounts=1", "ipv6_routed_prefix=2001:db8:cf00::/48")
	f.initScript("start")

	if routes := f.sh("ip -6 route show 2001:db8:cf00:2501::/64"); !strings.Contains(routes, "via 2001:db8:cf00:2301::2") {
		t.Fatalf("expected routed OpenWrt LAN route before purge, got: %q", routes)
	}

	if _, err := f.s.Run("sh", postrm, "purge"); err != nil {
		t.Fatalf("postrm purge: %v", err)
	}
	if routes := strings.TrimSpace(f.sh("ip -6 route show 2001:db8:cf00:2501::/64")); routes != "" {
		t.Errorf("routed OpenWrt LAN route left after postrm purge: %q", routes)
	}
	if tables := f.nftTables(); len(tables) != 0 {
		t.Errorf("nft tables left after postrm purge: %v", tables)
	}
	if files := common.HandleFiles(f.s); len(files) != 0 {
		t.Errorf("/run/cuttlefish not empty after postrm purge: %v", files)
	}
	if p := f.waitDnsmasq(func(s string) bool { return s == "" }); p != "" {
		t.Errorf("dnsmasq still running after postrm purge:\n%s", p)
	}
}

// TestStaticIPv6EgressDetectionControlsDnsServerAndMarker checks that when the
// host has kernel IPv6 enabled (disable_ipv6=0) but no IPv6 default route and
// no explicit dns6_servers override, start_ipv6 keeps ULA + RA prefix
// advertisements on cvd-* while omitting option6:dns-server and leaving
// /run/cuttlefish/ipv6-egress absent (so OpenWrt dnsmasq and RIL do not blackhole
// DNS queries to unreachable IPv6 servers), whereas when an IPv6 default route
// is present, start_ipv6 advertises option6:dns-server and writes
// /run/cuttlefish/ipv6-egress.
func TestStaticIPv6EgressDetectionControlsDnsServerAndMarker(t *testing.T) {
	t.Run("no_default_route_omits_dns_server_and_marker", func(t *testing.T) {
		f := newIPv6Fixture(t)
		f.writeDefaults("num_cvd_accounts=1")
		base := f.snapshot()

		out, err := f.s.Run("sh", f.script, "start")
		if err != nil {
			t.Fatalf("start: %v", err)
		}
		if want := "no IPv6 default route on host; omitting IPv6 DNS servers"; !strings.Contains(out.Stderr, want) {
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

		for _, ifname := range []string{"cvd-ebr", "cvd-wbr", "cvd-wifiap-01"} {
			cmdline := f.sh(fmt.Sprintf("tr '\\0' ' ' < /proc/$(cat /run/cuttlefish-dnsmasq-ra-%s.pid)/cmdline", ifname))
			if !strings.Contains(cmdline, "--enable-ra") {
				t.Errorf("RA dnsmasq on %s lacks --enable-ra: %s", ifname, cmdline)
			}
			if strings.Contains(cmdline, "option6:dns-server") {
				t.Errorf("RA dnsmasq on %s unexpectedly advertised option6:dns-server without host IPv6 default route: %s", ifname, cmdline)
			}
		}

		files := common.HandleFiles(f.s)
		if !slices.Contains(files, "ipv6-enabled") {
			t.Errorf("expected ipv6-enabled marker in /run/cuttlefish, got: %v", files)
		}
		if slices.Contains(files, "ipv6-egress") {
			t.Errorf("unexpected ipv6-egress marker in /run/cuttlefish without host IPv6 default route: %v", files)
		}

		f.initScript("stop")
		f.requireNoLeak(base)
	})

	t.Run("with_default_route_emits_dns_server_and_marker", func(t *testing.T) {
		f := newIPv6Fixture(t)
		f.writeDefaults("num_cvd_accounts=1")
		f.sh(`set -e
ip link add eth0 type dummy
echo 0 > /proc/sys/net/ipv6/conf/eth0/accept_dad
ip link set eth0 up
ip -6 addr add 2001:db8:ffff::2/64 dev eth0
ip -6 route add default via 2001:db8:ffff::1 dev eth0`)
		base := f.snapshot()

		f.initScript("start")

		for _, ifname := range []string{"cvd-ebr", "cvd-wbr", "cvd-wifiap-01"} {
			cmdline := f.sh(fmt.Sprintf("tr '\\0' ' ' < /proc/$(cat /run/cuttlefish-dnsmasq-ra-%s.pid)/cmdline", ifname))
			want := "--dhcp-option=option6:dns-server,2001:4860:4860::8888,2001:4860:4860::8844"
			if !strings.Contains(cmdline, want) {
				t.Errorf("RA dnsmasq on %s lacks %q when host has IPv6 default route: %s", ifname, want, cmdline)
			}
		}

		files := common.HandleFiles(f.s)
		if !slices.Contains(files, "ipv6-egress") {
			t.Errorf("expected ipv6-egress marker in /run/cuttlefish when host has IPv6 default route, got: %v", files)
		}
		if got := strings.TrimSpace(f.sh("cat /run/cuttlefish/ipv6-egress")); got != "2001:4860:4860::8888,2001:4860:4860::8844" {
			t.Errorf("/run/cuttlefish/ipv6-egress = %q, want 2001:4860:4860::8888,2001:4860:4860::8844", got)
		}

		f.initScript("stop")
		f.requireNoLeak(base)
	})
}
