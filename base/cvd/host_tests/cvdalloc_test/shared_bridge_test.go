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
	"testing"

	"github.com/google/android-cuttlefish/base/cvd/host_tests/common"
	"github.com/google/go-cmp/cmp"
)

// Start two instances, stop one, and check that the shared bridges, gateways
// and dnsmasq are untouched.
func TestCvdallocSharedBridgeRelease(t *testing.T) {
	wantBoth := phaseState{
		Ifaces: []netIface{
			{Name: "cvd-pi-ebr", Addr: "192.168.192.1/24"},
			{Name: "cvd-pi-wbr", Addr: "192.168.160.1/24"},
			{Name: "cvd-pi-etap1", Master: "cvd-pi-ebr", Tun: true},
			{Name: "cvd-pi-wtap1", Master: "cvd-pi-wbr", Tun: true},
			{Name: "cvd-pi-mtap1", Addr: "192.168.144.1/30", Tun: true},
			{Name: "cvd-pi-wifiap1", Addr: "192.168.176.1/30", Tun: true},
			{Name: "cvd-pi-etap2", Master: "cvd-pi-ebr", Tun: true},
			{Name: "cvd-pi-wtap2", Master: "cvd-pi-wbr", Tun: true},
			{Name: "cvd-pi-mtap2", Addr: "192.168.144.5/30", Tun: true},
			{Name: "cvd-pi-wifiap2", Addr: "192.168.176.5/30", Tun: true},
		},
		NftTables: []common.NftTable{
			{Family: "ip", Name: "cvdalloc_1", Flags: []string{"owner"}},
			{Family: "ip", Name: "cvdalloc_2", Flags: []string{"owner"}},
		},
		NftChains: []common.NftChain{
			{Family: "ip", Table: "cvdalloc_1", Name: "postrouting", Type: "nat", Hook: "postrouting"},
			{Family: "ip", Table: "cvdalloc_2", Name: "postrouting", Type: "nat", Hook: "postrouting"},
		},
		Masquerades: map[string][]string{
			"cvdalloc_1": {"192.168.144.0/30", "192.168.160.0/24", "192.168.176.0/30", "192.168.192.0/24"},
			"cvdalloc_2": {"192.168.144.4/30", "192.168.160.0/24", "192.168.176.4/30", "192.168.192.0/24"},
		},
		DnsmasqIfaces: []string{"cvd-pi-ebr", "cvd-pi-wbr"},
	}

	wantOnlyTwo := phaseState{
		Ifaces: []netIface{
			{Name: "cvd-pi-ebr", Addr: "192.168.192.1/24"},
			{Name: "cvd-pi-wbr", Addr: "192.168.160.1/24"},
			{Name: "cvd-pi-etap2", Master: "cvd-pi-ebr", Tun: true},
			{Name: "cvd-pi-wtap2", Master: "cvd-pi-wbr", Tun: true},
			{Name: "cvd-pi-mtap2", Addr: "192.168.144.5/30", Tun: true},
			{Name: "cvd-pi-wifiap2", Addr: "192.168.176.5/30", Tun: true},
		},
		NftTables: []common.NftTable{{Family: "ip", Name: "cvdalloc_2", Flags: []string{"owner"}}},
		NftChains: []common.NftChain{
			{Family: "ip", Table: "cvdalloc_2", Name: "postrouting", Type: "nat", Hook: "postrouting"},
		},
		Masquerades: map[string][]string{
			"cvdalloc_2": {"192.168.144.4/30", "192.168.160.0/24", "192.168.176.4/30", "192.168.192.0/24"},
		},
		DnsmasqIfaces: []string{"cvd-pi-ebr", "cvd-pi-wbr"},
	}

	opts := cmpOptions()

	s := common.NewSandbox(t)
	c := common.NewCvdalloc(t, s)
	base := snapshot(t, s)

	inst1, err := c.StartInstance(1)
	if err != nil {
		t.Fatalf("start instance 1: %v", err)
	}
	inst2, err := c.StartInstance(2)
	if err != nil {
		t.Fatalf("start instance 2: %v", err)
	}
	if diff := cmp.Diff(wantBoth, observe(t, s), opts); diff != "" {
		t.Errorf("host state with both instances (-want +got):\n%s", diff)
	}

	if err := c.StopInstance(inst1); err != nil {
		t.Fatalf("stop instance 1: %v", err)
	}
	if diff := cmp.Diff(wantOnlyTwo, observe(t, s), opts); diff != "" {
		t.Errorf("host state after releasing instance 1 (-want +got):\n%s", diff)
	}

	if err := c.StopInstance(inst2); err != nil {
		t.Fatalf("stop instance 2: %v", err)
	}
	checkReleased(t, s, base)
}
