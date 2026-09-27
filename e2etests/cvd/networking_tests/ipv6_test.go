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
	"encoding/json"
	"flag"
	"fmt"
	"net/netip"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"testing"
	"time"

	e2etests "github.com/google/android-cuttlefish/e2etests/cvd/common"
)

// Image capabilities. The IPv6 host setup of cuttlefish-host-resources
// (static mode) works with any image, but some guest adapters only get IPv6
// when the image carries the matching AOSP change. The Bazel targets in
// BUILD.bazel set these flags; the test never probes the image to decide
// what to check.
var (
	imageRilIPv6 = flag.Bool("image_ril_ipv6", false,
		"The image's reference RIL requests IPV4V6 PDP contexts and configures "+
			"the IPv6 address, route and DNS it gets from modem_simulator on buried_eth0.")
	imageOpenwrtIPv6 = flag.Bool("image_openwrt_ipv6", false,
		"The image connects Wi-Fi at boot and its OpenWrt build advertises the "+
			"fd00:cf:25::/48 LAN prefix with NAT66 on the WAN side "+
			"(docs/networking/openwrt_0_default_config_ipv6.patch).")
	imageManagesEth1 = flag.Bool("image_manages_eth1", false,
		"Android runs a network (IpClient) on eth1. Phone images do not; there the "+
			"test only checks the kernel SLAAC state of eth1.")
)

// Static-mode IPv6 plan of cuttlefish-host-resources. <i> is the instance
// number in hex.
const (
	mobilePrefixFmt    = "fd00:cf:21:%x::/64" // cvd-mtap-<i>, address from the RIL.
	wifiBridgePrefix   = "fd00:cf:22::/64"    // cvd-wbr (bridged Wi-Fi tap).
	wifiApPrefixFmt    = "fd00:cf:23:%x::/64" // cvd-wifiap-<i> (OpenWrt WAN).
	ethernetPrefix     = "fd00:cf:24::/64"    // cvd-ebr.
	openwrtLanPrefix   = "fd00:cf:25::/48"    // OpenWrt LAN, behind OpenWrt NAT66.
	documentationRange = "2001:db8::/32"      // Must never appear in the guest.

	ipv6EgressTarget = "2001:4860:4860::8888"
	ipv6Timeout      = 60 * time.Second
	pollInterval     = 2 * time.Second
)

// deviceInstance holds the parts of cuttlefish_config.json the test needs.
type deviceInstance struct {
	num               int
	useBridgedWifiTap bool
}

// readDeviceInstance reads the instance created by CVDCreate. CVDCreate runs
// `cvd create` with HOME set to the test directory, which is also the current
// directory.
func readDeviceInstance() (deviceInstance, error) {
	const path = "cuttlefish_runtime/cuttlefish_config.json"
	raw, err := os.ReadFile(path)
	if err != nil {
		return deviceInstance{}, fmt.Errorf("reading %s: %w", path, err)
	}
	var config struct {
		Instances map[string]struct {
			UseBridgedWifiTap bool `json:"use_bridged_wifi_tap"`
		} `json:"instances"`
	}
	if err := json.Unmarshal(raw, &config); err != nil {
		return deviceInstance{}, fmt.Errorf("parsing %s: %w", path, err)
	}
	if len(config.Instances) != 1 {
		return deviceInstance{}, fmt.Errorf("%s has %d instances, want 1", path, len(config.Instances))
	}
	d := deviceInstance{}
	for key, instance := range config.Instances {
		if d.num, err = strconv.Atoi(key); err != nil {
			return deviceInstance{}, fmt.Errorf("instance key %q in %s: %w", key, path, err)
		}
		d.useBridgedWifiTap = instance.UseBridgedWifiTap
	}
	return d, nil
}

// mobileIPv4 returns the guest address and gateway of buried_eth0, following
// create_interface() in cuttlefish-base.cuttlefish-host-resources.init.
func (d deviceInstance) mobileIPv4() (addr, gateway string) {
	base, n := "192.168.97", d.num
	if n > 64 {
		base, n = "192.168.93", n-64
	}
	return fmt.Sprintf("%s.%d/30", base, 4*n-2), fmt.Sprintf("%s.%d", base, 4*n-3)
}

// guestAdapter describes the IPv6 and IPv4 state expected on one guest
// interface.
type guestAdapter struct {
	iface string
	// ipv6 enables the IPv6 checks.
	ipv6 bool
	// prefix is the prefix that the guest address must be in.
	prefix netip.Prefix
	// exactAddr, if valid, is the only acceptable address (RIL-assigned).
	exactAddr netip.Addr
	// ra is true when the default route comes from a Router Advertisement,
	// false when the RIL installs it.
	ra bool
	// androidNetwork is true when Android runs a network on the interface, so
	// LinkProperties must carry the IPv6 address and an IPv6 DNS server.
	androidNetwork bool
	// hostPrefix is the source prefix that the host masquerades (NAT66).
	hostPrefix netip.Prefix
	// ipv4Addr and ipv4Gateway, if set, are the IPv4 address and default
	// gateway that must stay on the interface. ipv4Any accepts any address
	// and gateway.
	ipv4Addr    string
	ipv4Gateway string
	ipv4Any     bool
}

// guestAdapters returns the adapters of the device selected by the image
// capability flags.
func guestAdapters(d deviceInstance) []guestAdapter {
	mobile := netip.MustParsePrefix(fmt.Sprintf(mobilePrefixFmt, d.num))
	mobileAddr, mobileGateway := d.mobileIPv4()
	wifiHost := netip.MustParsePrefix(fmt.Sprintf(wifiApPrefixFmt, d.num))
	if d.useBridgedWifiTap {
		wifiHost = netip.MustParsePrefix(wifiBridgePrefix)
	}
	eth1 := guestAdapter{
		iface:          "eth1",
		ipv6:           true,
		prefix:         netip.MustParsePrefix(ethernetPrefix),
		ra:             true,
		androidNetwork: *imageManagesEth1,
		hostPrefix:     netip.MustParsePrefix(ethernetPrefix),
		ipv4Any:        *imageManagesEth1,
	}
	adapters := []guestAdapter{
		eth1,
		{
			iface:          "buried_eth0",
			ipv6:           *imageRilIPv6,
			prefix:         mobile,
			exactAddr:      mobile.Addr().Next().Next(),
			ra:             false,
			androidNetwork: true,
			hostPrefix:     mobile,
			ipv4Addr:       mobileAddr,
			ipv4Gateway:    mobileGateway,
		},
	}
	if *imageOpenwrtIPv6 {
		lan := netip.MustParsePrefix(openwrtLanPrefix)
		if d.useBridgedWifiTap {
			lan = netip.MustParsePrefix(wifiBridgePrefix)
		}
		adapters = append(adapters, guestAdapter{
			iface:          "wlan0",
			ipv6:           true,
			prefix:         lan,
			ra:             true,
			androidNetwork: true,
			hostPrefix:     wifiHost,
			ipv4Any:        true,
		})
	}
	return adapters
}

// guestShell runs a read-only command in the guest.
func guestShell(c *e2etests.TestContext, cmd string) (string, error) {
	out, err := c.RunCmd("adb", "shell", cmd)
	return out.Stdout, err
}

// waitForGuestOutput polls a read-only guest command until match returns a
// non-empty string or the timeout expires. RA, DAD and RIL data call setup
// complete asynchronously after boot.
func waitForGuestOutput(c *e2etests.TestContext, cmd string, match func(string) string) (string, error) {
	deadline := time.Now().Add(ipv6Timeout)
	last := ""
	for {
		out, err := guestShell(c, cmd)
		if err == nil {
			if m := match(out); m != "" {
				return m, nil
			}
			last = out
		}
		if time.Now().After(deadline) {
			return "", fmt.Errorf("timed out after %v running %q, last output:\n%s", ipv6Timeout, cmd, last)
		}
		time.Sleep(pollInterval)
	}
}

// globalIPv6Addrs parses `ip -6 -o addr show scope global`.
func globalIPv6Addrs(out string) []netip.Addr {
	var addrs []netip.Addr
	for _, line := range strings.Split(out, "\n") {
		fields := strings.Fields(line)
		if len(fields) < 4 || fields[2] != "inet6" {
			continue
		}
		if p, err := netip.ParsePrefix(fields[3]); err == nil {
			addrs = append(addrs, p.Addr())
		}
	}
	return addrs
}

// waitForIPv6Addr waits for the expected global address on the adapter.
func waitForIPv6Addr(c *e2etests.TestContext, a guestAdapter) (netip.Addr, error) {
	out, err := waitForGuestOutput(c, "ip -6 -o addr show dev "+a.iface+" scope global", func(out string) string {
		for _, addr := range globalIPv6Addrs(out) {
			if a.exactAddr.IsValid() && addr == a.exactAddr {
				return addr.String()
			}
			if !a.exactAddr.IsValid() && a.prefix.Contains(addr) {
				return addr.String()
			}
		}
		return ""
	})
	if err != nil {
		return netip.Addr{}, err
	}
	return netip.MustParseAddr(out), nil
}

// waitForIPv6DefaultRoute waits for the IPv6 default route of the adapter.
// The route is in the per-network table that Android (or the kernel, for
// interfaces Android does not manage) uses, so all tables are searched.
func waitForIPv6DefaultRoute(c *e2etests.TestContext, a guestAdapter) (string, error) {
	return waitForGuestOutput(c, "ip -6 route show table all", func(out string) string {
		for _, line := range strings.Split(out, "\n") {
			if !strings.HasPrefix(line, "default via ") || !strings.Contains(line, " dev "+a.iface+" ") {
				continue
			}
			isRA := strings.Contains(line, " proto ra ")
			if a.ra && isRA && strings.HasPrefix(line, "default via fe80:") {
				return line
			}
			if !a.ra && !isRA {
				return line
			}
		}
		return ""
	})
}

// raRoutes returns the routes on iface that a Router Advertisement installed.
func raRoutes(c *e2etests.TestContext, iface string) ([]string, error) {
	out, err := guestShell(c, "ip -6 route show table all")
	if err != nil {
		return nil, err
	}
	var routes []string
	for _, line := range strings.Split(out, "\n") {
		if strings.Contains(line, " dev "+iface+" ") && strings.Contains(line, " proto ra ") {
			routes = append(routes, line)
		}
	}
	return routes, nil
}

// bracketList returns the items of the first "<key>: [ a b ]" in s.
func bracketList(s, key string) []string {
	start := strings.Index(s, key+": [")
	if start < 0 {
		return nil
	}
	rest := s[start+len(key)+3:]
	end := strings.Index(rest, "]")
	if end < 0 {
		return nil
	}
	return strings.Fields(strings.ReplaceAll(rest[:end], ",", " "))
}

// waitForLinkProperties waits until the LinkProperties of the Android network
// on iface carry addr and an IPv6 DNS server. ConnectivityService pushes these
// DNS servers to the DNS resolver.
func waitForLinkProperties(c *e2etests.TestContext, iface string, addr netip.Addr) (string, error) {
	return waitForGuestOutput(c, "dumpsys connectivity", func(out string) string {
		for _, line := range strings.Split(out, "\n") {
			if !strings.Contains(line, "NetworkAgentInfo{") || !strings.Contains(line, "InterfaceName: "+iface+" ") {
				continue
			}
			hasAddr := false
			for _, la := range bracketList(line, "LinkAddresses") {
				if p, err := netip.ParsePrefix(la); err == nil && p.Addr() == addr {
					hasAddr = true
				}
			}
			var dns6 []string
			for _, d := range bracketList(line, "DnsAddresses") {
				if a, err := netip.ParseAddr(strings.TrimPrefix(d, "/")); err == nil && a.Is6() && !a.Is4In6() {
					dns6 = append(dns6, a.String())
				}
			}
			if hasAddr && len(dns6) > 0 {
				return strings.Join(dns6, " ")
			}
		}
		return ""
	})
}

// waitForIPv4 waits for the IPv4 address and default route of the adapter.
func waitForIPv4(c *e2etests.TestContext, a guestAdapter) (string, error) {
	addr, err := waitForGuestOutput(c, "ip -4 -o addr show dev "+a.iface, func(out string) string {
		for _, line := range strings.Split(out, "\n") {
			fields := strings.Fields(line)
			if len(fields) < 4 || fields[2] != "inet" {
				continue
			}
			if a.ipv4Any || fields[3] == a.ipv4Addr {
				return fields[3]
			}
		}
		return ""
	})
	if err != nil {
		return "", err
	}
	route, err := waitForGuestOutput(c, "ip -4 route show table all", func(out string) string {
		for _, line := range strings.Split(out, "\n") {
			if !strings.HasPrefix(line, "default via ") || !strings.Contains(line, " dev "+a.iface+" ") {
				continue
			}
			if a.ipv4Any || strings.HasPrefix(line, "default via "+a.ipv4Gateway+" ") {
				return line
			}
		}
		return ""
	})
	if err != nil {
		return "", err
	}
	return addr + "; " + route, nil
}

// launchDevice fetches and launches the phone image used by the IPv6 tests.
func launchDevice(t *testing.T, c *e2etests.TestContext) deviceInstance {
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
	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatal(err)
	}
	d, err := readDeviceInstance()
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("instance %d, use_bridged_wifi_tap=%v", d.num, d.useBridgedWifiTap)
	return d
}

// requireHostStaticIPv6 skips t when the host kernel has IPv6 disabled or
// cuttlefish-host-resources has not provisioned the static-mode ULA prefix on
// cvd-ebr (for example, on a host running an older cuttlefish-base package).
func requireHostStaticIPv6(t *testing.T) {
	t.Helper()
	if raw, err := os.ReadFile("/proc/sys/net/ipv6/conf/all/disable_ipv6"); err == nil && strings.TrimSpace(string(raw)) == "1" {
		t.Skip("skipping IPv6 check: host /proc/sys/net/ipv6/conf/all/disable_ipv6 is 1")
	}
	out, err := exec.Command("ip", "-6", "-o", "addr", "show", "dev", "cvd-ebr", "scope", "global").CombinedOutput()
	if err != nil {
		t.Skipf("skipping IPv6 check: cannot query cvd-ebr IPv6 addresses (%v: %s)", err, strings.TrimSpace(string(out)))
	}
	ethPrefix := netip.MustParsePrefix(ethernetPrefix)
	for _, addr := range globalIPv6Addrs(string(out)) {
		if ethPrefix.Contains(addr) {
			return
		}
	}
	t.Skipf("skipping IPv6 check: host cvd-ebr lacks an address in %s (output: %s)", ethPrefix, strings.TrimSpace(string(out)))
}

// TestIPv6Provisioning checks the IPv6 configuration that the static-mode
// host setup (cuttlefish-host-resources with its default settings) gives each
// guest adapter, and that IPv4 is unchanged. It needs no IPv6 upstream on the
// host, so it also runs on IPv4-only hosts.
//
// The test only reads guest and host state. It never adds addresses, routes
// or rules and never changes settings, so it sees what Android set up.
//
//   - eth1: SLAAC address in fd00:cf:24::/64 and an RA default route. Phone
//     images do not run an Android network on eth1, so only the kernel state
//     is checked unless --image_manages_eth1 is set.
//   - buried_eth0: IPv4 address and gateway of the instance. With
//     --image_ril_ipv6, also the RIL address fd00:cf:21:<i>::2, a RIL (not RA)
//     default route and IPv6 DNS in LinkProperties.
//   - wlan0 (--image_openwrt_ipv6 only): SLAAC address in the OpenWrt LAN
//     prefix fd00:cf:25::/48 (fd00:cf:22::/64 with a bridged Wi-Fi tap), an
//     RA default route, IPv6 DNS in LinkProperties and an IPv4 address.
//   - No guest address is in the 2001:db8::/32 documentation range.
func TestIPv6Provisioning(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	d := launchDevice(t, &c)

	for _, a := range guestAdapters(d) {
		t.Run(a.iface, func(t *testing.T) {
			if a.ipv4Addr != "" || a.ipv4Any {
				ipv4, err := waitForIPv4(&c, a)
				if err != nil {
					logDiagnostics(&c, t)
					t.Fatalf("IPv4 changed on %s (want address %q, gateway %q): %v", a.iface, a.ipv4Addr, a.ipv4Gateway, err)
				}
				t.Logf("IPv4: %s", ipv4)
			}

			if a.ipv6 {
				requireHostStaticIPv6(t)
				addr, err := waitForIPv6Addr(&c, a)
				if err != nil {
					logDiagnostics(&c, t)
					if a.exactAddr.IsValid() {
						t.Fatalf("no IPv6 address %s: %v", a.exactAddr, err)
					}
					t.Fatalf("no IPv6 address in %s: %v", a.prefix, err)
				}
				t.Logf("IPv6 address %s", addr)

				route, err := waitForIPv6DefaultRoute(&c, a)
				if err != nil {
					logDiagnostics(&c, t)
					t.Fatalf("no IPv6 default route (from RA: %v): %v", a.ra, err)
				}
				t.Logf("IPv6 default route: %s", route)

				if !a.ra {
					// The host sends no RAs on this segment; the RIL owns the
					// configuration.
					routes, err := raRoutes(&c, a.iface)
					if err != nil {
						t.Fatal(err)
					}
					if len(routes) > 0 {
						t.Errorf("unexpected RA routes on %s:\n%s", a.iface, strings.Join(routes, "\n"))
					}
				}

				if a.androidNetwork {
					dns, err := waitForLinkProperties(&c, a.iface, addr)
					if err != nil {
						logDiagnostics(&c, t)
						t.Fatalf("LinkProperties of %s lack %s or an IPv6 DNS server: %v", a.iface, addr, err)
					}
					t.Logf("IPv6 DNS servers in LinkProperties: %s", dns)
				}
			} else {
				t.Logf("IPv6 checks of %s need an image capability flag; checking IPv4 only", a.iface)
			}
		})
	}

	t.Run("NoDocumentationPrefix", func(t *testing.T) {
		out, err := guestShell(&c, "ip -6 -o addr show scope global")
		if err != nil {
			t.Fatal(err)
		}
		doc := netip.MustParsePrefix(documentationRange)
		for _, addr := range globalIPv6Addrs(out) {
			if doc.Contains(addr) {
				t.Errorf("guest has address %s in %s", addr, doc)
			}
		}
	})
}

// checkHostNat66 checks that the host masquerades the adapter's source
// prefix. Reading nftables needs CAP_NET_ADMIN; without it the ping result is
// the evidence.
func checkHostNat66(t *testing.T, c *e2etests.TestContext, prefix netip.Prefix) {
	out, err := c.RunCmd("nft", "list", "table", "ip6", "cuttlefish_nat6")
	if err != nil {
		t.Logf("cannot read host nftables without privileges (%v); relying on the guest ping", err)
		return
	}
	for _, line := range strings.Split(out.Stdout, "\n") {
		_, after, ok := strings.Cut(line, "ip6 saddr ")
		if !ok || !strings.Contains(line, "masquerade") {
			continue
		}
		fields := strings.Fields(after)
		if len(fields) == 0 {
			continue
		}
		if natPrefix, err := netip.ParsePrefix(fields[0]); err == nil &&
			natPrefix.Contains(prefix.Addr()) && natPrefix.Bits() <= prefix.Bits() {
			t.Logf("host NAT66 rule covering %s: %s", prefix, strings.TrimSpace(line))
			return
		}
	}
	t.Errorf("host table ip6 cuttlefish_nat6 has no masquerade rule covering %s:\n%s", prefix, out.Stdout)
}

// TestIPv6Nat66Egress checks off-link IPv6 from each Android network through
// the host NAT66. It needs an IPv6 upstream on the host, so its Bazel target
// carries the requires_ipv6_egress tag. It checks the adapters that the image
// capability flags enable and fails when none is enabled.
func TestIPv6Nat66Egress(t *testing.T) {
	requireHostStaticIPv6(t)
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	d := launchDevice(t, &c)

	var adapters []guestAdapter
	for _, a := range guestAdapters(d) {
		if a.ipv6 && a.androidNetwork {
			adapters = append(adapters, a)
		}
	}
	if len(adapters) == 0 {
		t.Fatal("no Android network with IPv6 selected; set --image_ril_ipv6, --image_openwrt_ipv6 or --image_manages_eth1")
	}

	for _, a := range adapters {
		t.Run(a.iface, func(t *testing.T) {
			addr, err := waitForIPv6Addr(&c, a)
			if err != nil {
				logDiagnostics(&c, t)
				t.Fatalf("no IPv6 address in %s: %v", a.prefix, err)
			}
			// ping6 binds to the interface, so netd's per-network rules
			// route it through that network.
			cmd := fmt.Sprintf("ping6 -c 3 -I %s %s", a.iface, ipv6EgressTarget)
			out, err := waitForGuestOutput(&c, cmd, func(out string) string {
				if strings.Contains(out, " 0% packet loss") {
					return out
				}
				return ""
			})
			if err != nil {
				logDiagnostics(&c, t)
				t.Fatalf("IPv6 egress from %s (%s) failed: %v", a.iface, addr, err)
			}
			// ping6 prints "PING <dst>(<dst>) from <src> <iface>: ...".
			src := netip.Addr{}
			if _, after, ok := strings.Cut(out, ") from "); ok {
				if fields := strings.Fields(after); len(fields) > 0 {
					src, _ = netip.ParseAddr(fields[0])
				}
			}
			if !a.prefix.Contains(src) {
				t.Errorf("ping6 source %v is not in %s:\n%s", src, a.prefix, out)
			}
			checkHostNat66(t, &c, a.hostPrefix)
		})
	}
}
