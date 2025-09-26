// Copyright 2025 Google LLC
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
	"path/filepath"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type ListScreenRecordingsActionOpts struct {
	Selector    cvd.InstanceSelector
	ExecContext exec.ExecContext
}

type ListScreenRecordingsAction struct {
	selector cvd.InstanceSelector
	cvdCLI   *cvd.CLI
}

func NewListScreenRecordingsAction(opts ListScreenRecordingsActionOpts) *ListScreenRecordingsAction {
	return &ListScreenRecordingsAction{
		selector: opts.Selector,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func toApiv1ListRecordingsResponse(recordings []string) *apiv1.ListScreenRecordingsResponse {
	response := apiv1.ListScreenRecordingsResponse{
		ScreenRecordings: []string{},
	}
	for _, recording := range recordings {
		response.ScreenRecordings = append(response.ScreenRecordings, filepath.Base(recording))
	}
	return &response
}

func (a *ListScreenRecordingsAction) Run() (*apiv1.ListScreenRecordingsResponse, error) {
	recordings, err := a.cvdCLI.LazySelectInstance(a.selector).ListScreenRecordings()
	if err != nil {
		return nil, operator.NewInternalError("failed to list screen recordings", err)
	}
	return toApiv1ListRecordingsResponse(recordings), nil
}
