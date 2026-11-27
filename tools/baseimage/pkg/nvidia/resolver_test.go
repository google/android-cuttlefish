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

package nvidia

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
)

func TestParseGCPLTSVersion(t *testing.T) {
	sample := `
VERSION_MAP = {
    "prod": {"driver": {"version": "595.91.07"}},
    "lts": {
        "driver": {"version": "580.178.04"},
        "cuda": {"driver": "580.126.20"},
    },
}`
	tests := []struct {
		name    string
		input   string
		want    string
		wantErr bool
	}{
		{name: "standard", input: sample, want: "580.178.04"},
		{name: "single_quotes", input: `VERSION_MAP = {'lts': {'driver': {'version': '620.70.10'}}}`, want: "620.70.10"},
		{name: "missing_lts", input: `VERSION_MAP = {"prod": {"driver": {"version": "595.91.07"}}}`, wantErr: true},
		{name: "missing_driver", input: `VERSION_MAP = {"lts": {"cuda": {"driver": "580.126.20"}}}`, wantErr: true},
		{name: "unterminated", input: `VERSION_MAP = {"lts": {"driver": {"version": "580.178.04"}`, wantErr: true},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := parseGCPLTSVersion(tc.input)
			if (err != nil) != tc.wantErr {
				t.Fatalf("parseGCPLTSVersion(%s) err = %v, wantErr = %v", tc.name, err, tc.wantErr)
			}
			if !tc.wantErr && got != tc.want {
				t.Errorf("parseGCPLTSVersion(%s) = %q, want %q", tc.name, got, tc.want)
			}
		})
	}
}

func TestValidateOpts(t *testing.T) {
	tests := []struct {
		name    string
		opts    ResolveOpts
		wantErr bool
	}{
		{name: "defaults", opts: ResolveOpts{Arch: gce.ArchX86, Branch: BranchGCPLTS}},
		{name: "explicit_version", opts: ResolveOpts{Arch: gce.ArchArm, Version: "580.196.11", Branch: BranchGCPLTS}},
		{name: "numeric_branch", opts: ResolveOpts{Arch: gce.ArchX86, Branch: "620", BranchExplicit: true}},
		{name: "unknown_arch", opts: ResolveOpts{Arch: gce.ArchUnknown}, wantErr: true},
		{name: "version_and_branch_conflict", opts: ResolveOpts{Arch: gce.ArchX86, Version: "580.196.11", Branch: "620", BranchExplicit: true}, wantErr: true},
		{name: "bad_version", opts: ResolveOpts{Arch: gce.ArchX86, Version: "580.196.11;id"}, wantErr: true},
		{name: "bad_branch", opts: ResolveOpts{Arch: gce.ArchX86, Branch: "beta"}, wantErr: true},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if err := ValidateOpts(tc.opts); (err != nil) != tc.wantErr {
				t.Errorf("ValidateOpts(%+v) err = %v, wantErr = %v", tc.opts, err, tc.wantErr)
			}
		})
	}
}

func newTestServer(t *testing.T) (*httptest.Server, *Client) {
	t.Helper()
	var srv *httptest.Server
	srv = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/releases.json":
			fmt.Fprintf(w, `{
				"620": {
					"type": "lts branch",
					"driver_info": [{
						"release_version": "620.70.10",
						"architectures": ["x86_64", "aarch64"],
						"runfile_url": {
							"x86_64": "%s/run/620.70.10/NVIDIA-Linux-x86_64-620.70.10.run",
							"aarch64": "%s/run/620.70.10/NVIDIA-Linux-aarch64-620.70.10.run"
						}
					}]
				},
				"580": {
					"type": "lts branch",
					"driver_info": [
						{
							"release_version": "580.196.07",
							"architectures": ["x86_64", "aarch64"],
							"runfile_url": {
								"x86_64": "%s/run/580.196.07/NVIDIA-Linux-x86_64-580.196.07.run",
								"aarch64": "%s/run/580.196.07/NVIDIA-Linux-aarch64-580.196.07.run"
							}
						},
						{
							"release_version": "580.196.11",
							"architectures": ["x86_64", "aarch64"],
							"runfile_url": {
								"x86_64": "%s/run/580.196.11/NVIDIA-Linux-x86_64-580.196.11.run",
								"aarch64": "%s/run/580.196.11/NVIDIA-Linux-aarch64-580.196.11.run"
							}
						}
					]
				}
			}`, srv.URL, srv.URL, srv.URL, srv.URL, srv.URL, srv.URL)
		case "/config.py":
			fmt.Fprint(w, `VERSION_MAP = {"lts": {"driver": {"version": "580.178.04"}}}`)
		case "/run/570.158.01/NVIDIA-Linux-x86_64-570.158.01.run":
			w.WriteHeader(http.StatusOK)
		default:
			w.WriteHeader(http.StatusNotFound)
		}
	}))
	t.Cleanup(srv.Close)

	return srv, &Client{
		HTTPClient:         srv.Client(),
		ReleasesURL:        srv.URL + "/releases.json",
		GCPConfigURL:       srv.URL + "/config.py",
		RunfileURLTemplate: srv.URL + "/run/%s/NVIDIA-Linux-%s-%s.run",
	}
}

func TestClientResolve(t *testing.T) {
	srv, client := newTestServer(t)

	tests := []struct {
		name        string
		opts        ResolveOpts
		wantVersion string
		wantURL     string
		wantErr     bool
	}{
		{
			name:        "default_gcp_lts_x86",
			opts:        ResolveOpts{Arch: gce.ArchX86, Branch: BranchGCPLTS},
			wantVersion: "580.196.11",
			wantURL:     srv.URL + "/run/580.196.11/NVIDIA-Linux-x86_64-580.196.11.run",
		},
		{
			name:        "default_gcp_lts_arm64",
			opts:        ResolveOpts{Arch: gce.ArchArm, Branch: BranchGCPLTS},
			wantVersion: "580.196.11",
			wantURL:     srv.URL + "/run/580.196.11/NVIDIA-Linux-aarch64-580.196.11.run",
		},
		{
			name:        "latest_lts",
			opts:        ResolveOpts{Arch: gce.ArchX86, Branch: BranchLTS, BranchExplicit: true},
			wantVersion: "620.70.10",
			wantURL:     srv.URL + "/run/620.70.10/NVIDIA-Linux-x86_64-620.70.10.run",
		},
		{
			name:        "explicit_branch",
			opts:        ResolveOpts{Arch: gce.ArchX86, Branch: "580", BranchExplicit: true},
			wantVersion: "580.196.11",
			wantURL:     srv.URL + "/run/580.196.11/NVIDIA-Linux-x86_64-580.196.11.run",
		},
		{
			name:    "explicit_branch_missing",
			opts:    ResolveOpts{Arch: gce.ArchX86, Branch: "999", BranchExplicit: true},
			wantErr: true,
		},
		{
			name:        "explicit_version_in_releases",
			opts:        ResolveOpts{Arch: gce.ArchX86, Version: "580.196.07"},
			wantVersion: "580.196.07",
			wantURL:     srv.URL + "/run/580.196.07/NVIDIA-Linux-x86_64-580.196.07.run",
		},
		{
			name:        "explicit_older_version_head_fallback",
			opts:        ResolveOpts{Arch: gce.ArchX86, Version: "570.158.01"},
			wantVersion: "570.158.01",
			wantURL:     srv.URL + "/run/570.158.01/NVIDIA-Linux-x86_64-570.158.01.run",
		},
		{
			name:    "explicit_nonexistent_version",
			opts:    ResolveOpts{Arch: gce.ArchX86, Version: "999.99.99"},
			wantErr: true,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := client.Resolve(t.Context(), tc.opts)
			if (err != nil) != tc.wantErr {
				t.Fatalf("client.Resolve(%+v) err = %v, wantErr = %v", tc.opts, err, tc.wantErr)
			}
			if !tc.wantErr && (got.Version != tc.wantVersion || got.RunfileURL != tc.wantURL) {
				t.Errorf("client.Resolve(%s) = %+v, want version=%q url=%q",
					tc.name, got, tc.wantVersion, tc.wantURL)
			}
		})
	}
}
