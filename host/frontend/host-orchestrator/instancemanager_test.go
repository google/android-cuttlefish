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

package main

import (
	"errors"
	"testing"
)

func TestCreateCVDInvalidRequests(t *testing.T) {
	im := &InstanceManager{}
	var validRequest = func() *CreateCVDRequest {
		return &CreateCVDRequest{
			BuildInfo: &BuildInfo{
				BuildID: "1234",
				Target:  "aosp_cf_x86_64_phone-userdebug",
			},
			FetchCVDBuildID:     "9999",
			BuildAPIAccessToken: "fakeaccesstoken",
		}
	}
	// Make sure the valid request is indeed valid.
  if err := validateRequest(validRequest()); err != nil {
		t.Fatalf("the valid request is not valid")
	}
	var tests = []struct {
		corruptRequest func(r *CreateCVDRequest)
	}{
		{func(r *CreateCVDRequest) { r.BuildInfo = nil }},
		{func(r *CreateCVDRequest) { r.BuildInfo.BuildID = "" }},
		{func(r *CreateCVDRequest) { r.BuildInfo.Target = "" }},
		{func(r *CreateCVDRequest) { r.FetchCVDBuildID = "" }},
		{func(r *CreateCVDRequest) { r.BuildAPIAccessToken = "" }},
	}

	for _, test := range tests {
		req := validRequest()
		test.corruptRequest(req)
		_, err := im.CreateCVD(req)
		var appErr *AppError
		if !errors.As(err, &appErr) {
			t.Errorf("unexpected error <<\"%v\">>, want \"%T\"", err, appErr)
		}
	}
}
