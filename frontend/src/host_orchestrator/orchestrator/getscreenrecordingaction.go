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
	"fmt"
	"net/http"
	"path/filepath"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type GetScreenRecordingActionOpts struct {
	Selector    cvd.InstanceSelector
	Recording   string
	ExecContext exec.ExecContext
	Writer      http.ResponseWriter
	Request     *http.Request
}

type GetScreenRecordingAction struct {
	selector  cvd.InstanceSelector
	recording string
	cvdCLI    *cvd.CLI
	writer    http.ResponseWriter
	request   *http.Request
}

func NewGetScreenRecordingAction(opts GetScreenRecordingActionOpts) *GetScreenRecordingAction {
	return &GetScreenRecordingAction{
		selector:  opts.Selector,
		recording: opts.Recording,
		cvdCLI:    cvd.NewCLI(opts.ExecContext),
		writer:    opts.Writer,
		request:   opts.Request,
	}
}

func (a *GetScreenRecordingAction) Run() {
	recordings, err := a.cvdCLI.LazySelectInstance(a.selector).ListScreenRecordings()
	if err != nil {
		replyJSONErr(a.writer, operator.NewInternalError("failed to get screen recording", err))
		return
	}
	for _, recording := range recordings {
		if filepath.Base(recording) == a.recording {
			a.writer.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=\"%s\"", a.recording))
			http.ServeFile(a.writer, a.request, recording)
			return
		}
	}
	replyJSONErr(a.writer, operator.NewNotFoundError(fmt.Sprintf("Requested screen recording not found: %s", a.recording), nil))
}
