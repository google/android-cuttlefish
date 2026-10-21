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

// Device-free tests of the parsing and plan helpers used by ipv6_test.go.
// Run alone with: go test ./cvd/networking_tests -run 'TestIPv6Helper'

import (
	"net/netip"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestIPv6HelperGlobalIPv6Addrs(t *testing.T) {
	out := "3: eth1    inet6 fd00:cf:24::5054:ff:fe12:3456/64 scope global dynamic mngtmpaddr \\       valid_lft 86394sec\n" +
		"3: eth1    inet6 fe80::1/64 scope link \\       valid_lft forever\n" +
		"4: buried_eth0    inet6 fd00:cf:21:a::2/64 scope global \\       valid_lft forever\n" +
		"garbage\n"
	got := globalIPv6Addrs(out)
	want := []netip.Addr{
		netip.MustParseAddr("fd00:cf:24::5054:ff:fe12:3456"),
		netip.MustParseAddr("fe80::1"),
		netip.MustParseAddr("fd00:cf:21:a::2"),
	}
	if diff := cmp.Diff(want, got, cmp.Comparer(func(a, b netip.Addr) bool { return a == b })); diff != "" {
		t.Errorf("globalIPv6Addrs (-want +got):\n%s", diff)
	}
}

func TestIPv6HelperBracketList(t *testing.T) {
	line := "NetworkAgentInfo{... InterfaceName: buried_eth0 LinkAddresses: [ 192.168.97.2/30,fd00:cf:21:1::2/64 ] DnsAddresses: [ /8.8.8.8,/2001:4860:4860::8888 ] ...}"
	if diff := cmp.Diff([]string{"192.168.97.2/30", "fd00:cf:21:1::2/64"}, bracketList(line, "LinkAddresses")); diff != "" {
		t.Errorf("LinkAddresses (-want +got):\n%s", diff)
	}
	if diff := cmp.Diff([]string{"/8.8.8.8", "/2001:4860:4860::8888"}, bracketList(line, "DnsAddresses")); diff != "" {
		t.Errorf("DnsAddresses (-want +got):\n%s", diff)
	}
	if got := bracketList(line, "Routes"); got != nil {
		t.Errorf("missing key: got %v, want nil", got)
	}
}

func TestIPv6HelperMobileIPv4(t *testing.T) {
	for _, c := range []struct {
		num           int
		addr, gateway string
	}{
		{1, "192.168.97.2/30", "192.168.97.1"},
		{64, "192.168.97.254/30", "192.168.97.253"},
		{65, "192.168.93.2/30", "192.168.93.1"},
	} {
		addr, gw := deviceInstance{num: c.num}.mobileIPv4()
		if addr != c.addr || gw != c.gateway {
			t.Errorf("instance %d: got (%s, %s), want (%s, %s)", c.num, addr, gw, c.addr, c.gateway)
		}
	}
}

// The expected mobile address is the one assemble_cvd derives from the host's
// fd00:cf:21:<i hex>::1/64 (MobileIpv6ConfigFromHostAddress): <prefix>::2.
func TestIPv6HelperGuestAdapters(t *testing.T) {
	defer func(r, o, e bool) { *imageRilIPv6, *imageOpenwrtIPv6, *imageManagesEth1 = r, o, e }(
		*imageRilIPv6, *imageOpenwrtIPv6, *imageManagesEth1)

	*imageRilIPv6, *imageOpenwrtIPv6, *imageManagesEth1 = false, false, false
	a := guestAdapters(deviceInstance{num: 10})
	if len(a) != 2 || a[0].iface != "eth1" || a[1].iface != "buried_eth0" {
		t.Fatalf("default adapters: %+v", a)
	}
	if a[1].ipv6 {
		t.Error("buried_eth0 IPv6 checked without --image_ril_ipv6")
	}
	if a[0].androidNetwork {
		t.Error("eth1 LinkProperties checked without --image_manages_eth1")
	}

	*imageRilIPv6, *imageOpenwrtIPv6 = true, true
	a = guestAdapters(deviceInstance{num: 10})
	if len(a) != 3 || a[2].iface != "wlan0" {
		t.Fatalf("adapters with all flags: %+v", a)
	}
	if want := netip.MustParseAddr("fd00:cf:21:a::2"); a[1].exactAddr != want {
		t.Errorf("buried_eth0 exact address %v, want %v", a[1].exactAddr, want)
	}
	if want := netip.MustParsePrefix("fd00:cf:23:a::/64"); a[2].hostPrefix != want {
		t.Errorf("wlan0 host prefix %v, want %v", a[2].hostPrefix, want)
	}
	if want := netip.MustParsePrefix("fd00:cf:25::/48"); a[2].prefix != want {
		t.Errorf("wlan0 prefix %v, want %v", a[2].prefix, want)
	}

	a = guestAdapters(deviceInstance{num: 3, useBridgedWifiTap: true})
	// wlan0 stays an OpenWrt LAN client; only the WAN segment moves to cvd-wbr.
	wantLAN := netip.MustParsePrefix("fd00:cf:25::/48")
	wantHost := netip.MustParsePrefix("fd00:cf:22::/64")
	if a[2].prefix != wantLAN || a[2].hostPrefix != wantHost {
		t.Errorf("bridged wlan0 prefix %v host %v, want %v host %v", a[2].prefix, a[2].hostPrefix, wantLAN, wantHost)
	}
}
