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
	"path/filepath"
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

func (s *Sandbox) Snapshot() HostState {
	hs := HostState{Sysctls: map[string]string{}}

	if out, err := s.Run("nft", "-j", "list", "ruleset"); err == nil {
		hs.Nft = parseNftRuleset(out.Stdout)
	} else {
		s.t.Logf("snapshot: nft list failed: %v", err)
	}
	if out, err := s.Run("ip", "-details", "-json", "link"); err == nil {
		hs.Links = parseLinks(out.Stdout)
	} else {
		s.t.Logf("snapshot: ip link failed: %v", err)
	}
	if out, err := s.Run("ip", "-json", "addr"); err == nil {
		hs.Addrs = parseAddrs(out.Stdout)
	} else {
		s.t.Logf("snapshot: ip addr failed: %v", err)
	}
	for key, path := range map[string]string{
		"net.ipv4.ip_forward":          "/proc/sys/net/ipv4/ip_forward",
		"net.ipv6.conf.all.forwarding": "/proc/sys/net/ipv6/conf/all/forwarding",
	} {
		if out, err := s.Run("cat", path); err == nil {
			hs.Sysctls[key] = strings.TrimSpace(out.Stdout)
		}
	}
	return hs
}

// We can only check for the pidfiles, so we just do that to make sure
// dnsmasq was invoked.
func (s *Sandbox) DnsmasqPidfileIfaces() []string {
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

func (s *Sandbox) HandleFiles() []string {
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
	sort.Slice(nft.Tables, func(i, j int) bool { return nft.Tables[i].less(nft.Tables[j]) })
	sort.Slice(nft.Chains, func(i, j int) bool { return nft.Chains[i].less(nft.Chains[j]) })
	sort.Slice(nft.Rules, func(i, j int) bool { return nft.Rules[i].less(nft.Rules[j]) })
	out.Nft = nft

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
	sort.Slice(out.Links, func(i, j int) bool { return out.Links[i].Ifname < out.Links[j].Ifname })

	for _, a := range hs.Addrs {
		na := Addr{Ifname: a.Ifname}
		for _, ai := range a.AddrInfo {
			if ai.Family == "inet6" && strings.HasPrefix(ai.Local, "fe80") {
				continue
			}
			na.AddrInfo = append(na.AddrInfo, AddrInfo{Family: ai.Family, Local: ai.Local, Prefixlen: ai.Prefixlen})
		}
		sort.Slice(na.AddrInfo, func(i, j int) bool { return na.AddrInfo[i].Local < na.AddrInfo[j].Local })
		out.Addrs = append(out.Addrs, na)
	}
	sort.Slice(out.Addrs, func(i, j int) bool { return out.Addrs[i].Ifname < out.Addrs[j].Ifname })

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
