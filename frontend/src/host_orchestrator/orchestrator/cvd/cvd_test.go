// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package cvd

import (
	"context"
	"os/exec"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestSliceItoa(t *testing.T) {
	tests := []struct {
		in  []uint32
		out []string
	}{
		{
			in:  []uint32{},
			out: []string{},
		},
		{
			in:  []uint32{79, 83, 89, 97},
			out: []string{"79", "83", "89", "97"},
		},
	}
	for _, tc := range tests {

		res := sliceItoa(tc.in)

		if diff := cmp.Diff(tc.out, res); diff != "" {
			t.Errorf("result mismatch (-want +got):\n%s", diff)
		}
	}
}

func TestLoadCmd(t *testing.T) {
	execCtx := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		return exec.CommandContext(ctx, name, args...)
	}
	cli := NewCLI(execCtx)
	tests := []struct {
		name string
		opts LoadOpts
		want []string
	}{
		{
			name: "defaults",
			opts: LoadOpts{},
			want: []string{CVDBin, "load", "config.json"},
		},
		{
			name: "build api base url",
			opts: LoadOpts{BuildAPIBaseURL: "https://example.com"},
			want: []string{CVDBin, "load", "config.json", "--override=fetch.api_base_url:https://example.com"},
		},
		{
			name: "fetch caching disabled with gce credentials",
			opts: LoadOpts{
				DisableFetchCaching: true,
				Credentials:         FetchCredentials{UseGCEServiceAccountCredentials: true},
			},
			want: []string{
				CVDBin, "load", "config.json",
				"--override=fetch.enable_caching:false",
				"--credential_source=gce",
			},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			cmd, err := cli.loadCmd("config.json", tc.opts)
			if err != nil {
				t.Fatalf("loadCmd() failed: %v", err)
			}
			if diff := cmp.Diff(tc.want, cmd.Args); diff != "" {
				t.Errorf("loadCmd() args mismatch (-want +got):\n%s", diff)
			}
		})
	}
}
