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

// Releasing one instance must leave the bridges, gateways, and dnsmasq shared
// with another running instance untouched.
func TestCvdallocSharedBridgeRelease(t *testing.T) {
	nftTables := []common.NftTable{
		{Family: "ip", Name: "cf_cvdalloc_nat"},
	}
	nftChains := []common.NftChain{
		{Family: "ip", Table: "cf_cvdalloc_nat", Name: "postrouting", Type: "nat", Hook: "postrouting"},
	}

	// Both instances allocated.
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
		NftTables: nftTables,
		NftChains: nftChains,
		Masquerades: []string{
			"192.168.144.0/30",
			"192.168.144.4/30",
			"192.168.160.0/24",
			"192.168.176.0/30",
			"192.168.176.4/30",
			"192.168.192.0/24",
			"192.168.96.0/24",
			"192.168.98.0/24",
		},
		DnsmasqIfaces: []string{"cvd-pi-ebr", "cvd-pi-wbr"},
	}

	// Instance 1 released; instance 2 and the shared bridges remain.
	wantOnlyTwo := phaseState{
		Ifaces: []netIface{
			{Name: "cvd-pi-ebr", Addr: "192.168.192.1/24"},
			{Name: "cvd-pi-wbr", Addr: "192.168.160.1/24"},
			{Name: "cvd-pi-etap2", Master: "cvd-pi-ebr", Tun: true},
			{Name: "cvd-pi-wtap2", Master: "cvd-pi-wbr", Tun: true},
			{Name: "cvd-pi-mtap2", Addr: "192.168.144.5/30", Tun: true},
			{Name: "cvd-pi-wifiap2", Addr: "192.168.176.5/30", Tun: true},
		},
		NftTables: nftTables,
		NftChains: nftChains,
		Masquerades: []string{
			"192.168.144.4/30",
			"192.168.160.0/24",
			"192.168.176.4/30",
			"192.168.192.0/24",
			"192.168.96.0/24",
			"192.168.98.0/24",
		},
		DnsmasqIfaces: []string{"cvd-pi-ebr", "cvd-pi-wbr"},
	}

	opts := cmpOptions()

	s := common.NewSandbox(t)
	defer s.Close()
	c := common.NewCvdalloc(t, s)

	if err := c.Setup(); err != nil {
		t.Fatalf("setup: %v", err)
	}

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
}
