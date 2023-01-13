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

package client

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	apiv1 "github.com/google/cloud-android-orchestration/api/v1"

	"github.com/google/go-cmp/cmp"
)

func TestRetryLogic(t *testing.T) {
	failsTotal := 2
	failsCounter := 0
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		opName := "op-foo"
		switch ep := r.Method + " " + r.URL.Path; ep {
		case "POST /hosts":
			writeOK(w, &apiv1.Operation{Name: opName})
		case "POST /operations/" + opName + "/:wait":
			if failsCounter < failsTotal {
				failsCounter++
				writeErr(w, 503)
				return
			}
			writeOK(w, &apiv1.HostInstance{Name: "foo"})
		default:
			panic("unexpected endpoint: " + ep)
		}

	}))
	defer ts.Close()
	opts := &ServiceOptions{
		BaseURL:       ts.URL,
		DumpOut:       io.Discard,
		RetryAttempts: 2,
		RetryDelay:    100 * time.Millisecond,
	}
	client, _ := NewService(opts)

	start := time.Now()
	host, _ := client.CreateHost(&apiv1.CreateHostRequest{})
	duration := time.Since(start)

	expected := &apiv1.HostInstance{Name: "foo"}
	if diff := cmp.Diff(expected, host); diff != "" {
		t.Errorf("standard output mismatch (-want +got):\n%s", diff)
	}
	if duration < opts.RetryDelay*2 {
		t.Error("duration faster than expected")
	}
}

func writeErr(w http.ResponseWriter, statusCode int) {
	write(w, &apiv1.Error{Code: statusCode}, statusCode)
}

func writeOK(w http.ResponseWriter, data any) {
	write(w, data, http.StatusOK)
}

func write(w http.ResponseWriter, data any, statusCode int) {
	w.WriteHeader(statusCode)
	w.Header().Set("Content-Type", "application/json")
	encoder := json.NewEncoder(w)
	encoder.Encode(data)
}
