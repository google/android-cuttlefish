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
	"github.com/google/go-cmp/cmp/cmpopts"
)

type netIface struct {
	Name   string
	Addr   string // primary IPv4 CIDR, "" if none
	Master string // bridge it is enslaved to, "" if none
	Tun    bool   // tap opened with vnet_hdr
}

type phaseState struct {
	Ifaces        []netIface
	NftTables     []common.NftTable
	NftChains     []common.NftChain
	Masquerades   []string
	HandleFiles   []string
	DnsmasqIfaces []string
}

func TestCvdallocLifecycle(t *testing.T) {
	// A single instance, id=1.
	const id = 1

	// After "--setup"
	wantSetup := phaseState{
		NftTables: []common.NftTable{
			{Family: "ip", Name: "cf_cvdalloc_nat"},
		},
		NftChains: []common.NftChain{
			{Family: "ip", Table: "cf_cvdalloc_nat", Name: "postrouting", Type: "nat", Hook: "postrouting"},
		},
		Masquerades: []string{
			"192.168.160.0/24",
			"192.168.192.0/24",
			"192.168.96.0/24",
			"192.168.98.0/24",
		},
	}

	// After the instance allocates
	wantInstance := phaseState{
		Ifaces: []netIface{
			{Name: "cvd-pi-ebr", Addr: "192.168.192.1/24"},
			{Name: "cvd-pi-wbr", Addr: "192.168.160.1/24"},
			{Name: "cvd-pi-etap1", Master: "cvd-pi-ebr", Tun: true},
			{Name: "cvd-pi-wtap1", Master: "cvd-pi-wbr", Tun: true},
			{Name: "cvd-pi-mtap1", Addr: "192.168.144.1/30", Tun: true},
			{Name: "cvd-pi-wifiap1", Addr: "192.168.176.1/30", Tun: true},
		},
		NftTables: wantSetup.NftTables,
		NftChains: wantSetup.NftChains,
		Masquerades: []string{
			"192.168.144.0/30",
			"192.168.160.0/24",
			"192.168.176.0/30",
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

	base, err := common.Snapshot(s)
	if err != nil {
		t.Fatalf("snapshot base: %v", err)
	}

	// --- Phase 1: static setup ---
	if err := c.Setup(); err != nil {
		t.Fatalf("setup: %v", err)
	}
	if diff := cmp.Diff(wantSetup, observe(t, s), opts); diff != "" {
		t.Errorf("host state after --setup (-want +got):\n%s", diff)
	}

	// --- Phase 2: instance allocation (runtime) ---
	inst, err := c.StartInstance(id)
	if err != nil {
		t.Fatalf("start instance: %v", err)
	}
	if diff := cmp.Diff(wantInstance, observe(t, s), opts); diff != "" {
		t.Errorf("host state after instance allocation (-want +got):\n%s", diff)
	}

	// --- Phase 3: instance release ---
	if err := c.StopInstance(inst); err != nil {
		t.Fatalf("stop instance: %v", err)
	}
	if diff := cmp.Diff(wantSetup, observe(t, s), opts); diff != "" {
		t.Errorf("host state after instance release (-want +got):\n%s", diff)
	}

	// --- Phase 4: static teardown ---
	if err := c.Teardown(); err != nil {
		t.Fatalf("teardown: %v", err)
	}
	afterTeardown, err := common.Snapshot(s)
	if err != nil {
		t.Fatalf("snapshot afterTeardown: %v", err)
	}
	if diff := common.DiffState(common.Normalize(base), common.Normalize(afterTeardown)); diff != "" {
		t.Errorf("state leaked after teardown (-before +after):\n%s", diff)
	}
}

func observe(t *testing.T, s *common.Sandbox) phaseState {
	hs, err := common.Snapshot(s)
	if err != nil {
		t.Fatalf("snapshot: %v", err)
	}
	got := phaseState{
		NftTables:     hs.Nft.Tables,
		NftChains:     hs.Nft.Chains,
		Masquerades:   hs.Nft.MasqueradeSaddrs(),
		HandleFiles:   common.HandleFiles(s),
		DnsmasqIfaces: common.CvdallocDnsmasqIfaces(s),
	}
	for _, l := range hs.Links {
		if l.Ifname == "lo" {
			continue
		}
		got.Ifaces = append(got.Ifaces, netIface{
			Name:   l.Ifname,
			Addr:   hs.PrimaryIPv4(l.Ifname),
			Master: l.Master,
			Tun:    l.Kind() == "tun" && l.VnetHdr(),
		})
	}
	return got
}

func cmpOptions() cmp.Options {
	return cmp.Options{
		cmpopts.SortSlices(func(a, b netIface) bool { return a.Name < b.Name }),
		cmpopts.SortSlices(func(a, b common.NftTable) bool {
			if a.Family != b.Family {
				return a.Family < b.Family
			}
			return a.Name < b.Name
		}),
		cmpopts.SortSlices(func(a, b common.NftChain) bool {
			if a.Family != b.Family {
				return a.Family < b.Family
			}
			if a.Table != b.Table {
				return a.Table < b.Table
			}
			return a.Name < b.Name
		}),
		cmpopts.IgnoreFields(common.NftTable{}, "Handle"),
		cmpopts.IgnoreFields(common.NftChain{}, "Handle"),
	}
}
