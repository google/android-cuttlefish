// Copyright (C) 2025 The Android Open Source Project
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
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"testing"
	"time"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

const baseURL = "http://0.0.0.0:2080"

// IMPORTANT!!! This test requires modifying the Host Orchestrator
// Service default config at /etc/default/cuttlefish-host_orchestrator,
// having the following line
//
// ```
// orchestrator_android_build_url=http://localhost:8090
// ```

type State struct {
	ReceivedAccessToken string
}

func TestVerifyAccessToken(t *testing.T) {
	state := &State{}
	if err := startFakeBuildAPIServer(8090, state); err != nil {
		t.Fatalf("failed to start fake build api server: %v", err)
	}
	cases := []struct {
		name string
		f    func(srv hoclient.HostOrchestratorClient, token string)
	}{
		{
			name: "FetchArtifacts",
			f: func(srv hoclient.HostOrchestratorClient, token string) {
				r := &hoapi.FetchArtifactsRequest{
					AndroidCIBundle: &hoapi.AndroidCIBundle{
						Build: &hoapi.AndroidCIBuild{
							BuildID: "13150599",
							Target:  "aosp_cf_x86_64_phone-trunk_staging-userdebug",
						},
						Type: hoapi.MainBundleType,
					},
				}
				srv.FetchArtifacts(r, &hoclient.AccessTokenBuildAPICreds{AccessToken: token})
			},
		},
		{
			name: "CreateCVDWithEnvConfig",
			f: func(srv hoclient.HostOrchestratorClient, token string) {
				config := `
                  {
                    "instances": [
                      {
                        "vm": {
                          "memory_mb": 8192,
                          "setupwizard_mode": "OPTIONAL",
                          "cpus": 8
                        },
                        "disk": {
                          "default_build": "@ab/aosp_main/aosp_cf_x86_64_phone-trunk_staging-userdebug"
                        }
                      }
                    ]
                  }
                  `
				envConfig := make(map[string]interface{})
				if err := json.Unmarshal([]byte(config), &envConfig); err != nil {
					log.Fatalf("failed to unmarshall env config: %s", err)
				}
				req := &hoapi.CreateCVDRequest{EnvConfig: envConfig}
				srv.CreateCVD(req, &hoclient.AccessTokenBuildAPICreds{AccessToken: token})
			},
		},
	}
	srv := hoclient.NewHostOrchestratorClient(baseURL)
	for _, c := range cases {
		state.ReceivedAccessToken = ""

		c.f(srv, "foo")

		if diff := cmp.Diff("Bearer foo", state.ReceivedAccessToken); diff != "" {
			t.Fatalf("%s: access token mismatch (-want +got):\n%s", c.name, diff)
		}
	}
}

func startFakeBuildAPIServer(port int, state *State) error {
	address := fmt.Sprintf("localhost:%d", port)
	go func() {
		http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
			log.Printf("fake server: received url: %s", r.URL.String())
			state.ReceivedAccessToken = r.Header.Get("Authorization")
			w.WriteHeader(http.StatusOK)
		})
		log.Println(http.ListenAndServe(address, nil))
	}()
	_, err := net.DialTimeout("tcp", address, 30*time.Second)
	return err
}
