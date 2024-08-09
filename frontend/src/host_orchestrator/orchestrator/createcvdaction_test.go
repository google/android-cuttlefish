// Copyright 2023 Google LLC
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

package orchestrator

import (
	"errors"
	"testing"

	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

func TestCreateCVDInvalidRequestsEmptyFields(t *testing.T) {
	validRequest := func() *apiv1.CreateCVDRequest {
		return &apiv1.CreateCVDRequest{
			CVD: &apiv1.CVD{
				BuildSource: &apiv1.BuildSource{
					AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
						MainBuild: &apiv1.AndroidCIBuild{
							BuildID: "1234",
							Target:  "aosp_cf_x86_64_phone-trunk_staging-userdebug",
						},
					},
				},
			},
		}
	}
	// Make sure the valid request is indeed valid.
	if err := validateRequest(validRequest()); err != nil {
		t.Fatalf("the valid request is not valid")
	}
	var tests = []struct {
		corruptRequest func(r *apiv1.CreateCVDRequest)
	}{
		{func(r *apiv1.CreateCVDRequest) { r.CVD.BuildSource = nil }},
		{func(r *apiv1.CreateCVDRequest) { r.CVD.BuildSource.AndroidCIBuildSource = nil }},
		{func(r *apiv1.CreateCVDRequest) {
			r.CVD.BuildSource.AndroidCIBuildSource = nil
			r.CVD.BuildSource.UserBuildSource = &apiv1.UserBuildSource{ArtifactsDir: ""}
		}},
	}
	for _, test := range tests {
		req := validRequest()
		test.corruptRequest(req)
		opts := CreateCVDActionOpts{
			Request: req,
		}
		action := NewCreateCVDAction(opts)

		_, err := action.Run()

		var appErr *operator.AppError
		if !errors.As(err, &appErr) {
			t.Errorf("error type <<\"%T\">> not found in error chain", appErr)
		}
		var emptyFieldErr EmptyFieldError
		if !errors.As(err, &emptyFieldErr) {
			t.Errorf("error type <<\"%T\">> not found in error chain", emptyFieldErr)
		}
	}
}

type AlwaysFailsValidator struct{}

func (AlwaysFailsValidator) Validate() error {
	return errors.New("validation failed")
}

func TestCreateCVDFailsDueInvalidHost(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	execContext := execCtxAlwaysSucceeds
	paths := IMPaths{ArtifactsRootDir: dir + "/artifacts"}
	om := NewMapOM()
	opts := CreateCVDActionOpts{
		Request:          &apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}},
		HostValidator:    &AlwaysFailsValidator{},
		Paths:            paths,
		OperationManager: om,
		ExecContext:      execContext,
	}
	action := NewCreateCVDAction(opts)

	_, err := action.Run()

	if err == nil {
		t.Error("expected error")
	}
}
