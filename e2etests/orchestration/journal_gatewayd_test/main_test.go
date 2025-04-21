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
	"io"
	"net/http"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

const baseURL = "http://0.0.0.0:2080"

func TestHostOrchestratorServiceLogs(t *testing.T) {
	url := baseURL + "/_journal/entries?_SYSTEMD_UNIT=cuttlefish-host_orchestrator.service"

	res, err := http.Get(url)
	if err != nil {
		t.Fatal(err)
	}
	defer res.Body.Close()

	if diff := cmp.Diff(http.StatusOK, res.StatusCode); diff != "" {
		t.Errorf("status code mismatch (-want +got):\n%s", diff)
	}

	body, err := io.ReadAll(res.Body)
	if err != nil {
		t.Fatal(err)
	}

	const line = "Host Orchestrator is listening at http://0.0.0.0:2081"
	if !strings.Contains(string(body), line) {
		t.Fatalf("line: %q not found in host orchestrator service logs")

	}
}
