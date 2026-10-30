// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package internal

import (
	"strings"
	"testing"
)

func TestStderrRewriter(t *testing.T) {
	tests := []struct {
		name   string
		writes []string
		want   string
	}{
		{
			name:   "cvd logs",
			writes: []string{"`cvd logs`\n"},
			want:   "`podcvd logs`\n",
		},
		{
			name:   "multiple lines in one write",
			writes: []string{"`cvd logs`\n`cvd logs`\n"},
			want:   "`podcvd logs`\n`podcvd logs`\n",
		},
		{
			name:   "line split across writes",
			writes: []string{"`cvd", " logs`\n"},
			want:   "`podcvd logs`\n",
		},
		{
			name:   "line without newline",
			writes: []string{"`cvd logs`"},
			want:   "`podcvd logs`",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			var out strings.Builder
			r := NewStderrRewriter(&out, "")
			for _, w := range tc.writes {
				if n, err := r.Write([]byte(w)); n != len(w) || err != nil {
					t.Fatalf("Write(%q) = %d, %v; want %d, nil", w, n, err, len(w))
				}
			}
			if err := r.Flush(); err != nil {
				t.Fatalf("Flush() = %v; want nil", err)
			}
			if got := out.String(); got != tc.want {
				t.Errorf("output = %q; want %q", got, tc.want)
			}
		})
	}
}
