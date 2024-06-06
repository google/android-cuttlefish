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
	"regexp"
	"testing"

	apiv1 "github.com/google/cloud-android-orchestration/api/v1"

	"github.com/hashicorp/go-multierror"
)

func TestDeleteHosts(t *testing.T) {
	existingNames := map[string]struct{}{"bar": {}, "baz": {}}
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "DELETE" {
			panic("unexpected method: " + r.Method)
		}
		re := regexp.MustCompile(`^/hosts/(.*)$`)
		matches := re.FindStringSubmatch(r.URL.Path)
		if len(matches) != 2 {
			panic("unexpected path: " + r.URL.Path)
		}
		if _, ok := existingNames[matches[1]]; ok {
			writeOK(w, "")
		} else {
			writeErr(w, 404)
		}
	}))
	defer ts.Close()
	opts := &ServiceOptions{
		RootEndpoint: ts.URL,
		DumpOut:      io.Discard,
	}
	srv, _ := NewService(opts)

	err := srv.DeleteHosts([]string{"foo", "bar", "baz", "quz"})

	merr, _ := err.(*multierror.Error)
	errs := merr.WrappedErrors()
	if len(errs) != 2 {
		t.Errorf("expected 2, got: %d", len(errs))
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
