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

// Unit tests of the TestIPv6RoutedEcho helpers. They need no device.

import (
	"net/netip"
	"testing"
)

func TestRoutedEchoHelperPrefixFromHostAddr(t *testing.T) {
	for _, c := range []struct {
		addr string
		num  int
		want string // "" for an error
	}{
		{"2001:db8:cf00:2101::1", 1, "2001:db8:cf00::/48"},
		{"2001:db8:0:210a::1", 10, "2001:db8::/48"},
		{"2001:db8:cf00:2180::1", 128, "2001:db8:cf00::/48"},
		{"2001:db8:cf00:2102::1", 1, ""}, // other instance
		{"2001:db8:cf00:2301::1", 1, ""}, // OpenWrt WAN, not mobile
		{"fd00:cf:21:1::1", 1, ""},       // private mode (ULA)
		{"fd00:cf:0:2101::1", 1, ""},     // ULA with the routed layout
		{"fe80::2101:0:0:1", 1, ""},
	} {
		got, err := routedPrefixFromHostAddr(netip.MustParseAddr(c.addr), c.num)
		if c.want == "" {
			if err == nil {
				t.Errorf("routedPrefixFromHostAddr(%s, %d) = %s, want error", c.addr, c.num, got)
			}
			continue
		}
		if err != nil || got != netip.MustParsePrefix(c.want) {
			t.Errorf("routedPrefixFromHostAddr(%s, %d) = %s, %v, want %s", c.addr, c.num, got, err, c.want)
		}
	}
}

func TestRoutedEchoHelperInstancePrefix(t *testing.T) {
	p := netip.MustParsePrefix("2001:db8:cf00::/48")
	for _, c := range []struct {
		net  byte
		num  int
		want string
	}{
		{routedMobileNet, 1, "2001:db8:cf00:2101::/64"},
		{routedWifiLanNet, 10, "2001:db8:cf00:250a::/64"},
		{routedWifiLanNet, 128, "2001:db8:cf00:2580::/64"},
	} {
		if got := routedInstancePrefix(p, c.net, c.num); got != netip.MustParsePrefix(c.want) {
			t.Errorf("routedInstancePrefix(%s, %x, %d) = %s, want %s", p, c.net, c.num, got, c.want)
		}
	}
}

func TestRoutedEchoHelperEchoAddr(t *testing.T) {
	for _, c := range []struct {
		body, want string // want "" for an error
	}{
		{"2001:db8:cf00:2101::2\n", "2001:db8:cf00:2101::2"},
		{"2001:db8:cf00:2501:a:b:c:d", "2001:db8:cf00:2501:a:b:c:d"},
		{`{"ip":"2001:db8:cf00:2101::2"}`, "2001:db8:cf00:2101::2"},
		{"Your IP: 2001:db8::1, port 443", "2001:db8::1"},
		{"192.0.2.7", "192.0.2.7"},
		{"fe80::1%eth0", "fe80::1"},
		{"no address here", ""},
		{"", ""},
	} {
		got, err := echoAddr(c.body)
		if c.want == "" {
			if err == nil {
				t.Errorf("echoAddr(%q) = %s, want error", c.body, got)
			}
			continue
		}
		if err != nil || got != netip.MustParseAddr(c.want) {
			t.Errorf("echoAddr(%q) = %s, %v, want %s", c.body, got, err, c.want)
		}
	}
}
