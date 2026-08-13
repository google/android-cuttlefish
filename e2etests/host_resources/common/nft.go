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
	Family string `json:"family"`
	Name   string `json:"name"`
	Handle int    `json:"handle"`
}

func (t NftTable) less(o NftTable) bool {
	if t.Family != o.Family {
		return t.Family < o.Family
	}
	return t.Name < o.Name
}

type NftChain struct {
	Family string `json:"family"`
	Table  string `json:"table"`
	Name   string `json:"name"`
	Type   string `json:"type"`
	Hook   string `json:"hook"`
	Handle int    `json:"handle"`
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

// Masquerade and SaddrPrefix are derived from the rule's expression list,
// so this type is populated via convertNftRule.
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

// The types below mirror the schema of `nft -j list ruleset`. Each element of
// the top-level "nftables" array is an object with a single populated key
// ("table", "chain", or "rule".
//
// An abridged example:
//
//	{
//	  "nftables": [
//	    {"metainfo": {"version": "1.0.6", "json_schema_version": 1}},
//	    {"table": {"family": "ip", "name": "cuttlefish_nat", "handle": 2}},
//	    {"chain": {"family": "ip", "table": "cuttlefish_nat", "name": "postrouting",
//	               "handle": 1, "type": "nat", "hook": "postrouting", "prio": 100, "policy": "accept"}},
//	    {"rule": {"family": "ip", "table": "cuttlefish_nat", "chain": "postrouting", "handle": 4,
//	              "expr": [
//	                {"match": {"op": "==",
//	                           "left": {"payload": {"protocol": "ip", "field": "saddr"}},
//	                           "right": {"prefix": {"addr": "192.168.96.0", "len": 24}}}},
//	                {"masquerade": null}
//	              ]}}
//	  ]
//	}
type nftElement struct {
	Table *NftTable `json:"table"`
	Chain *NftChain `json:"chain"`
	Rule  *nftRule  `json:"rule"`
}

type nftRule struct {
	Family string    `json:"family"`
	Table  string    `json:"table"`
	Chain  string    `json:"chain"`
	Handle int       `json:"handle"`
	Expr   []nftExpr `json:"expr"`
}

type nftExpr struct {
	Masquerade json.RawMessage `json:"masquerade"`
	Match      *nftMatch       `json:"match"`
}

type nftMatch struct {
	Left  nftMatchLeft  `json:"left"`
	Right nftMatchRight `json:"right"`
}

type nftMatchLeft struct {
	Payload *nftPayload `json:"payload"`
}

type nftPayload struct {
	Field string `json:"field"`
}

type nftMatchRight struct {
	Prefix *nftPrefix `json:"prefix"`
}

type nftPrefix struct {
	Addr string `json:"addr"`
	Len  int    `json:"len"`
}

func parseNftRuleset(s string) NftRuleset {
	var top struct {
		Nftables []nftElement `json:"nftables"`
	}
	if err := json.Unmarshal([]byte(s), &top); err != nil {
		return NftRuleset{}
	}
	var rs NftRuleset
	for _, el := range top.Nftables {
		switch {
		case el.Table != nil:
			rs.Tables = append(rs.Tables, *el.Table)
		case el.Chain != nil:
			rs.Chains = append(rs.Chains, *el.Chain)
		case el.Rule != nil:
			rs.Rules = append(rs.Rules, convertNftRule(*el.Rule))
		}
	}
	return rs
}

// convertNftRule flattens the nft json rule schema into the NftRule domain type,
// deriving Masquerade and SaddrPrefix from the rule's expression list.
func convertNftRule(r nftRule) NftRule {
	nr := NftRule{Family: r.Family, Table: r.Table, Chain: r.Chain, Handle: r.Handle}
	for _, e := range r.Expr {
		if len(e.Masquerade) > 0 {
			nr.Masquerade = true
		}
		if e.Match != nil && e.Match.Left.Payload != nil && e.Match.Left.Payload.Field == "saddr" && e.Match.Right.Prefix != nil {
			nr.SaddrPrefix = fmt.Sprintf("%s/%d", e.Match.Right.Prefix.Addr, e.Match.Right.Prefix.Len)
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
