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

package common

import (
	"fmt"
	"path/filepath"
	"slices"
	"sort"
	"strings"

	"github.com/google/go-cmp/cmp"
)

type HostState struct {
	Nft     NftRuleset
	Links   []Link
	Addrs   []Addr
	Sysctls map[string]string
}

func Snapshot(s *Sandbox) (HostState, error) {
	hs := HostState{Sysctls: map[string]string{}}

	out, err := s.Run("nft", "-j", "list", "ruleset")
	if err != nil {
		return hs, fmt.Errorf("snapshot: nft list failed: %w", err)
	}
	hs.Nft = parseNftRuleset(out.Stdout)

	out, err = s.Run("ip", "-details", "-json", "link")
	if err != nil {
		return hs, fmt.Errorf("snapshot: ip link failed: %w", err)
	}
	hs.Links = parseLinks(out.Stdout)

	out, err = s.Run("ip", "-json", "addr")
	if err != nil {
		return hs, fmt.Errorf("snapshot: ip addr failed: %w", err)
	}
	hs.Addrs = parseAddrs(out.Stdout)

	for key, path := range map[string]string{
		"net.ipv4.ip_forward":          "/proc/sys/net/ipv4/ip_forward",
		"net.ipv6.conf.all.forwarding": "/proc/sys/net/ipv6/conf/all/forwarding",
	} {
		out, err := s.Run("cat", path)
		if err != nil {
			return hs, fmt.Errorf("snapshot: read sysctl %q failed: %w", key, err)
		}
		hs.Sysctls[key] = strings.TrimSpace(out.Stdout)
	}
	return hs, nil
}

// We can only check for the pidfiles, so we just do that to make sure
// dnsmasq was invoked.
func DnsmasqPidfileIfaces(s *Sandbox) []string {
	out, err := s.Run("sh", "-c", `ls -1 /run/cuttlefish-dnsmasq-*.pid 2>/dev/null || true`)
	if err != nil {
		return nil
	}
	var ifaces []string
	for _, line := range nonEmptyLines(out.Stdout) {
		base := filepath.Base(line)
		ifaces = append(ifaces, strings.TrimSuffix(strings.TrimPrefix(base, "cuttlefish-dnsmasq-"), ".pid"))
	}
	sort.Strings(ifaces)
	return ifaces
}

func HandleFiles(s *Sandbox) []string {
	out, err := s.Run("sh", "-c", `ls -1 /run/cuttlefish/ 2>/dev/null || true`)
	if err != nil {
		return nil
	}
	files := nonEmptyLines(out.Stdout)
	sort.Strings(files)
	return files
}

func DiffState(a, b HostState) string {
	return cmp.Diff(a, b)
}

// Normalize strips volatile fields (nft handles, ifindex, MAC, link-local IPv6)
// so two snapshots can be compared for leaks.
func Normalize(hs HostState) HostState {
	out := HostState{}

	// normalize the nftables state
	nft := NftRuleset{}
	nft.Tables = append(nft.Tables, hs.Nft.Tables...)
	nft.Chains = append(nft.Chains, hs.Nft.Chains...)
	nft.Rules = append(nft.Rules, hs.Nft.Rules...)
	for i := range nft.Tables {
		nft.Tables[i].Handle = 0
	}
	for i := range nft.Chains {
		nft.Chains[i].Handle = 0
	}
	for i := range nft.Rules {
		nft.Rules[i].Handle = 0
	}
	slices.SortFunc(nft.Tables, NftTable.compare)
	slices.SortFunc(nft.Chains, NftChain.compare)
	slices.SortFunc(nft.Rules, NftRule.compare)
	out.Nft = nft

	// normalize the ip link states
	for _, l := range hs.Links {
		nl := Link{Ifname: l.Ifname, Master: l.Master}
		if l.IsUp() {
			nl.Flags = []string{"UP"}
		}
		if l.LinkInfo != nil {
			nl.LinkInfo = &LinkInfo{InfoKind: l.LinkInfo.InfoKind}
			if l.LinkInfo.InfoData != nil {
				nl.LinkInfo.InfoData = &TunData{Type: l.LinkInfo.InfoData.Type, VnetHdr: l.LinkInfo.InfoData.VnetHdr}
			}
		}
		out.Links = append(out.Links, nl)
	}
	slices.SortFunc(out.Links, func(a, b Link) int { return strings.Compare(a.Ifname, b.Ifname) })

	// normalize the ip addr states
	for _, a := range hs.Addrs {
		na := Addr{Ifname: a.Ifname}
		for _, ai := range a.AddrInfo {
			if ai.Family == "inet6" && strings.HasPrefix(ai.Local, "fe80") {
				continue
			}
			na.AddrInfo = append(na.AddrInfo, AddrInfo{Family: ai.Family, Local: ai.Local, Prefixlen: ai.Prefixlen})
		}
		slices.SortFunc(na.AddrInfo, func(a, b AddrInfo) int { return strings.Compare(a.Local, b.Local) })
		out.Addrs = append(out.Addrs, na)
	}
	slices.SortFunc(out.Addrs, func(a, b Addr) int { return strings.Compare(a.Ifname, b.Ifname) })

	// explicitly exclude sysctls from the normalized version
	// so we can deterministically verify that we didn't leak
	// anything.

	return out
}

func nonEmptyLines(s string) []string {
	var out []string
	for _, l := range strings.Split(s, "\n") {
		l = strings.TrimSpace(l)
		if l != "" {
			out = append(out, l)
		}
	}
	return out
}
