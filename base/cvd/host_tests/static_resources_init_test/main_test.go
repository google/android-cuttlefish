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

// startState is the full network state expected after `start`.
type startState struct {
	Ifaces        []netIface
	NftTables     []common.NftTable
	NftChains     []common.NftChain
	Masquerades   []string
	Sysctls       map[string]string
	HandleFiles   []string
	DnsmasqIfaces []string
}

func TestStaticResourcesInit(t *testing.T) {
	cases := []struct {
		name string
		env  common.InitEnv
		want startState
	}{
		{
			name: "num_cvd_accounts=1",
			env:  common.InitEnv{NumCvdAccounts: 1},
			want: startState{
				Ifaces: []netIface{
					{Name: "cvd-ebr", Addr: "192.168.98.1/24"},
					{Name: "cvd-wbr", Addr: "192.168.96.1/24"},
					{Name: "cvd-etap-01", Master: "cvd-ebr", Tun: true},
					{Name: "cvd-wtap-01", Master: "cvd-wbr", Tun: true},
					{Name: "cvd-mtap-01", Addr: "192.168.97.1/30", Tun: true},
					{Name: "cvd-wifiap-01", Addr: "192.168.94.1/30", Tun: true},
				},
				NftTables: []common.NftTable{
					{Family: "ip", Name: "cuttlefish_nat"},
					{Family: "bridge", Name: "cuttlefish_bridge"},
				},
				NftChains: []common.NftChain{
					{Family: "ip", Table: "cuttlefish_nat", Name: "postrouting", Type: "nat", Hook: "postrouting"},
					{Family: "bridge", Table: "cuttlefish_bridge", Name: "prerouting", Type: "filter", Hook: "prerouting"},
					{Family: "bridge", Table: "cuttlefish_bridge", Name: "forward", Type: "filter", Hook: "forward"},
				},
				Masquerades:   []string{"192.168.94.0/30", "192.168.96.0/24", "192.168.97.0/30", "192.168.98.0/24"},
				Sysctls:       map[string]string{"net.ipv4.ip_forward": "1", "net.ipv6.conf.all.forwarding": "1"},
				HandleFiles:   []string{"masq-br-cvd-ebr.handle", "masq-br-cvd-wbr.handle", "masq-cvd-mtap-01.handle", "masq-cvd-wifiap-01.handle"},
				DnsmasqIfaces: []string{"cvd-ebr", "cvd-wbr"},
			},
		},
	}

	opts := cmp.Options{
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

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			s := common.NewSandbox(t)
			defer s.Close()
			sr := common.NewStaticResources(t, s)

			base, err := common.Snapshot(s)
			if err != nil {
				t.Fatalf("snapshot base: %v", err)
			}

			if err := sr.Start(tc.env); err != nil {
				t.Fatalf("start: %v", err)
			}

			if diff := cmp.Diff(tc.want, observe(t, s), opts); diff != "" {
				t.Errorf("host state after start (-want +got):\n%s", diff)
			}

			if err := sr.Stop(tc.env); err != nil {
				t.Fatalf("stop: %v", err)
			}
			afterStop, err := common.Snapshot(s)
			if err != nil {
				t.Fatalf("snapshot afterStop: %v", err)
			}
			if diff := common.DiffState(common.Normalize(base), common.Normalize(afterStop)); diff != "" {
				t.Errorf("state leaked after stop (-before +after):\n%s", diff)
			}

			if diff := cmp.Diff(tc.want.Sysctls, afterStop.Sysctls); diff != "" {
				t.Errorf("forwarding sysctls changed after stop (-want +got):\n%s", diff)
			}
		})
	}
}

func observe(t *testing.T, s *common.Sandbox) startState {
	hs, err := common.Snapshot(s)
	if err != nil {
		t.Fatalf("snapshot: %v", err)
	}
	got := startState{
		NftTables:     hs.Nft.Tables,
		NftChains:     hs.Nft.Chains,
		Masquerades:   hs.Nft.MasqueradeSaddrs(),
		Sysctls:       hs.Sysctls,
		HandleFiles:   common.HandleFiles(s),
		DnsmasqIfaces: common.DnsmasqPidfileIfaces(s),
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
