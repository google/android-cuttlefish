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
	"strings"
	"testing"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

const baseURL = "http://0.0.0.0:2080"

const envConfigStr = `
{
  "instances": [
    {
      "vm": {
        "memory_mb": 8192,
        "setupwizard_mode": "OPTIONAL",
        "cpus": 8
      },
      "disk": {
        "default_build": "@ab/git_main/cf_x86_64_phone-trunk_staging-userdebug",
        "download_img_zip": true
      }
    }
  ]
}
`

// IMPORTANT!!! This test requires modifying the Host Orchestrator
// Service default config at /etc/default/cuttlefish-host_orchestrator,
// having the following line
//
// ```
// build_api_credentials_use_gce_metadata=true
// ```
func TestUseGCEMetadata(t *testing.T) {
	srv := hoclient.NewHostOrchestratorClient(baseURL)
	envConfig := make(map[string]any)
	if err := json.Unmarshal([]byte(envConfigStr), &envConfig); err != nil {
		t.Fatal(err)
	}
	req := &hoapi.CreateCVDRequest{EnvConfig: envConfig}

	_, err := srv.CreateCVD(req, &hoclient.AccessTokenBuildAPICreds{})

	log.Printf("error: %s\n", err)
	const expected = "Could not resolve host: metadata.google.internal"
	if !strings.Contains(fmt.Sprintf("%s", err), expected) {
		t.Fatalf("%q part not found in error message", expected)
	}
}
