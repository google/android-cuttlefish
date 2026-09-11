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
	"encoding/json"
	"fmt"
)

// derived from `ip -details -json link`
type Link struct {
	Ifindex   int       `json:"ifindex"`
	Ifname    string    `json:"ifname"`
	Flags     []string  `json:"flags"`
	Operstate string    `json:"operstate"`
	Master    string    `json:"master"`
	Address   string    `json:"address"`
	LinkInfo  *LinkInfo `json:"linkinfo"`
}

type LinkInfo struct {
	InfoKind string   `json:"info_kind"`
	InfoData *TunData `json:"info_data"`
}

type TunData struct {
	Type    string `json:"type"`
	VnetHdr bool   `json:"vnet_hdr"`
	Group   string `json:"group"`
}

func (l Link) Kind() string {
	if l.LinkInfo != nil {
		return l.LinkInfo.InfoKind
	}
	return ""
}

func (l Link) IsUp() bool {
	for _, f := range l.Flags {
		if f == "UP" {
			return true
		}
	}
	return false
}

func (l Link) VnetHdr() bool {
	return l.LinkInfo != nil && l.LinkInfo.InfoData != nil && l.LinkInfo.InfoData.VnetHdr
}

// derived from `ip -json addr`.
type Addr struct {
	Ifname   string     `json:"ifname"`
	AddrInfo []AddrInfo `json:"addr_info"`
}

type AddrInfo struct {
	Family    string `json:"family"`
	Local     string `json:"local"`
	Prefixlen int    `json:"prefixlen"`
	Scope     string `json:"scope"`
}

func parseLinks(s string) []Link {
	var links []Link
	json.Unmarshal([]byte(s), &links)
	return links
}

func parseAddrs(s string) []Addr {
	var addrs []Addr
	json.Unmarshal([]byte(s), &addrs)
	return addrs
}

// the first non-link-local IPv4 CIDR on the interface, or ""
func (hs HostState) PrimaryIPv4(ifname string) string {
	for _, a := range hs.Addrs {
		if a.Ifname != ifname {
			continue
		}
		for _, ai := range a.AddrInfo {
			if ai.Family == "inet" {
				return fmt.Sprintf("%s/%d", ai.Local, ai.Prefixlen)
			}
		}
	}
	return ""
}
