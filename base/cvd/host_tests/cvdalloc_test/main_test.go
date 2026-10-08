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
	"time"

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
	Masquerades   map[string][]string // masqueraded source prefixes by nft table
	HandleFiles   []string
	DnsmasqIfaces []string
}

func instance1State() phaseState {
	return phaseState{
		Ifaces: []netIface{
			{Name: "cvd-pi-ebr", Addr: "192.168.192.1/24"},
			{Name: "cvd-pi-wbr", Addr: "192.168.160.1/24"},
			{Name: "cvd-pi-etap1", Master: "cvd-pi-ebr", Tun: true},
			{Name: "cvd-pi-wtap1", Master: "cvd-pi-wbr", Tun: true},
			{Name: "cvd-pi-mtap1", Addr: "192.168.144.1/30", Tun: true},
			{Name: "cvd-pi-wifiap1", Addr: "192.168.176.1/30", Tun: true},
		},
		NftTables: []common.NftTable{{Family: "ip", Name: "cvdalloc_1", Flags: []string{"owner"}}},
		NftChains: []common.NftChain{
			{Family: "ip", Table: "cvdalloc_1", Name: "postrouting", Type: "nat", Hook: "postrouting"},
		},
		Masquerades: map[string][]string{
			"cvdalloc_1": {"192.168.144.0/30", "192.168.160.0/24", "192.168.176.0/30", "192.168.192.0/24"},
		},
		DnsmasqIfaces: []string{"cvd-pi-ebr", "cvd-pi-wbr"},
	}
}

func TestCvdallocLifecycle(t *testing.T) {
	s := common.NewSandbox(t)
	c := common.NewCvdalloc(t, s)
	base := snapshot(t, s)

	inst, err := c.StartInstance(1)
	if err != nil {
		t.Fatalf("start instance 1: %v", err)
	}
	if diff := cmp.Diff(instance1State(), observe(t, s), cmpOptions()); diff != "" {
		t.Errorf("host state after allocation (-want +got):\n%s", diff)
	}

	if err := c.StopInstance(inst); err != nil {
		t.Fatalf("stop instance 1: %v", err)
	}
	checkReleased(t, s, base)
}

func snapshot(t *testing.T, s *common.Sandbox) common.HostState {
	t.Helper()
	hs, err := common.Snapshot(s)
	if err != nil {
		t.Fatalf("snapshot: %v", err)
	}
	return hs
}

func observe(t *testing.T, s *common.Sandbox) phaseState {
	t.Helper()
	hs := snapshot(t, s)
	got := phaseState{
		NftTables:     hs.Nft.Tables,
		NftChains:     hs.Nft.Chains,
		Masquerades:   hs.Nft.MasqueradeSaddrsByTable(),
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

func checkReleased(t *testing.T, s *common.Sandbox, base common.HostState) {
	t.Helper()
	// Give the dnsmasq shims time to exit and remove their pidfiles.
	deadline := time.Now().Add(5 * time.Second)
	for len(common.CvdallocDnsmasqIfaces(s)) > 0 && time.Now().Before(deadline) {
		time.Sleep(50 * time.Millisecond)
	}
	if ifaces := common.CvdallocDnsmasqIfaces(s); len(ifaces) > 0 {
		t.Errorf("dnsmasq still running for %v after release", ifaces)
	}
	after := snapshot(t, s)
	if diff := common.DiffState(common.Normalize(base), common.Normalize(after)); diff != "" {
		t.Errorf("state leaked after release (-before +after):\n%s", diff)
	}
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
