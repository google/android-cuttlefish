// Copyright 2024 Google LLC
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
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/go-cmp/cmp"
)

func TestRetryLogic(t *testing.T) {
	failsTotal := 2
	failsCounter := 0
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if failsCounter < failsTotal {
			failsCounter++
			writeErr(w, http.StatusServiceUnavailable)
			return
		}
		writeOK(w, &apiv1.CVD{Name: "foo"})
		return
	}))
	defer ts.Close()
	helper := HTTPHelper{
		Client:       &http.Client{},
		RootEndpoint: ts.URL,
		Dumpster:     io.Discard,
	}
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusServiceUnavailable},
		RetryDelay:  1 * time.Millisecond,
		MaxWait:     1 * time.Minute,
	}
	res := &apiv1.CVD{}

	err := helper.NewPostRequest("", nil).JSONResDoWithRetries(res, retryOpts)
	if err != nil {
		t.Fatal(err)
	}

	expected := &apiv1.CVD{Name: "foo"}
	if diff := cmp.Diff(expected, res); diff != "" {
		t.Errorf("host instance mismatch (-want +got):\n%s", diff)
	}
}

func TestRetryLogicMaxWaitElapsed(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(100 * time.Millisecond)
		writeErr(w, http.StatusServiceUnavailable)
	}))
	defer ts.Close()
	helper := HTTPHelper{
		Client:       &http.Client{},
		RootEndpoint: ts.URL,
		Dumpster:     io.Discard,
	}
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusServiceUnavailable},
		MaxWait:     10 * time.Millisecond,
	}
	res := &apiv1.CVD{}

	err := helper.NewPostRequest("", nil).JSONResDoWithRetries(res, retryOpts)

	if err == nil {
		t.Fatal("expected max wait elapsed error")
	}
}
