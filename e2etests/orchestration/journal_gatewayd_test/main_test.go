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
	"net/http"
	"testing"

	"github.com/google/go-cmp/cmp"
)

const baseURL = "http://0.0.0.0:2080"

func TestHostOrchestratorServiceLogs(t *testing.T) {
	paths := []string{
		"/_journal/entries?_SYSTEMD_UNIT=cuttlefish-host_orchestrator.service",
		"/hostlogs/cuttlefish-host_orchestrator.service",
		"/hostlogs/cuttlefish-operator.service",
		"/hostlogs/kernel",
	}
	for _, p := range paths {
		res, err := http.Get(baseURL + p)
		if err != nil {
			t.Fatal(err)
		}
		defer res.Body.Close()

		if diff := cmp.Diff(http.StatusOK, res.StatusCode); diff != "" {
			t.Errorf("GET %q status code mismatch (-want +got):\n%s", p, diff)
		}
	}
}
