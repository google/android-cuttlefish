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
	"sort"
)

type NftRuleset struct {
	Tables []NftTable
	Chains []NftChain
	Rules  []NftRule
}

type NftTable struct {
	Family string
	Name   string
	Handle int
}

func (t NftTable) less(o NftTable) bool {
	if t.Family != o.Family {
		return t.Family < o.Family
	}
	return t.Name < o.Name
}

type NftChain struct {
	Family string
	Table  string
	Name   string
	Type   string
	Hook   string
	Handle int
}

func (c NftChain) less(o NftChain) bool {
	if c.Family != o.Family {
		return c.Family < o.Family
	}
	if c.Table != o.Table {
		return c.Table < o.Table
	}
	return c.Name < o.Name
}

type NftRule struct {
	Family      string
	Table       string
	Chain       string
	Handle      int
	Masquerade  bool
	SaddrPrefix string
}

func (r NftRule) less(o NftRule) bool {
	if r.Family != o.Family {
		return r.Family < o.Family
	}
	if r.Table != o.Table {
		return r.Table < o.Table
	}
	if r.Chain != o.Chain {
		return r.Chain < o.Chain
	}
	if r.SaddrPrefix != o.SaddrPrefix {
		return r.SaddrPrefix < o.SaddrPrefix
	}
	return !r.Masquerade && o.Masquerade
}

func parseNftRuleset(s string) NftRuleset {
	var top struct {
		Nftables []map[string]json.RawMessage `json:"nftables"`
	}
	if err := json.Unmarshal([]byte(s), &top); err != nil {
		return NftRuleset{}
	}
	var rs NftRuleset
	for _, el := range top.Nftables {
		if raw, ok := el["table"]; ok {
			var t struct {
				Family string `json:"family"`
				Name   string `json:"name"`
				Handle int    `json:"handle"`
			}
			if json.Unmarshal(raw, &t) == nil {
				rs.Tables = append(rs.Tables, NftTable{Family: t.Family, Name: t.Name, Handle: t.Handle})
			}
		}
		if raw, ok := el["chain"]; ok {
			var c struct {
				Family string `json:"family"`
				Table  string `json:"table"`
				Name   string `json:"name"`
				Type   string `json:"type"`
				Hook   string `json:"hook"`
				Handle int    `json:"handle"`
			}
			if json.Unmarshal(raw, &c) == nil {
				rs.Chains = append(rs.Chains, NftChain{Family: c.Family, Table: c.Table, Name: c.Name, Type: c.Type, Hook: c.Hook, Handle: c.Handle})
			}
		}
		if raw, ok := el["rule"]; ok {
			rs.Rules = append(rs.Rules, parseNftRule(raw))
		}
	}
	return rs
}

func parseNftRule(raw json.RawMessage) NftRule {
	var r struct {
		Family string            `json:"family"`
		Table  string            `json:"table"`
		Chain  string            `json:"chain"`
		Handle int               `json:"handle"`
		Expr   []json.RawMessage `json:"expr"`
	}
	json.Unmarshal(raw, &r)
	nr := NftRule{Family: r.Family, Table: r.Table, Chain: r.Chain, Handle: r.Handle}
	for _, e := range r.Expr {
		var m map[string]json.RawMessage
		if json.Unmarshal(e, &m) != nil {
			continue
		}
		if _, ok := m["masquerade"]; ok {
			nr.Masquerade = true
		}
		if mraw, ok := m["match"]; ok {
			var match struct {
				Left struct {
					Payload *struct {
						Field string `json:"field"`
					} `json:"payload"`
				} `json:"left"`
				Right json.RawMessage `json:"right"`
			}
			if json.Unmarshal(mraw, &match) == nil && match.Left.Payload != nil && match.Left.Payload.Field == "saddr" {
				var pfx struct {
					Prefix *struct {
						Addr string `json:"addr"`
						Len  int    `json:"len"`
					} `json:"prefix"`
				}
				if json.Unmarshal(match.Right, &pfx) == nil && pfx.Prefix != nil {
					nr.SaddrPrefix = fmt.Sprintf("%s/%d", pfx.Prefix.Addr, pfx.Prefix.Len)
				}
			}
		}
	}
	return nr
}

func (rs NftRuleset) MasqueradeSaddrs() []string {
	var out []string
	for _, r := range rs.Rules {
		if r.Masquerade && r.SaddrPrefix != "" {
			out = append(out, r.SaddrPrefix)
		}
	}
	sort.Strings(out)
	return out
}
