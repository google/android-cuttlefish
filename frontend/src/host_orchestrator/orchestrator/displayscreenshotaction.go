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
	"encoding/base64"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type DisplayScreenshotActionOpts struct {
	Request          *apiv1.DisplayScreenshotRequest
	Selector         cvd.Selector
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type DisplayScreenshotAction struct {
	req      *apiv1.DisplayScreenshotRequest
	selector cvd.Selector
	paths    IMPaths
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewDisplayScreenshotAction(opts DisplayScreenshotActionOpts) *DisplayScreenshotAction {
	return &DisplayScreenshotAction{
		req:      opts.Request,
		selector: opts.Selector,
		paths:    opts.Paths,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func (a *DisplayScreenshotAction) Run() (apiv1.Operation, error) {
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		result.Value, result.Error = a.createScreenshot(op)
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op)
	return op, nil
}

func (a *DisplayScreenshotAction) createScreenshot(op apiv1.Operation) (*apiv1.DisplayScreenshotResponse, error) {
	tempdir, err := ioutil.TempDir("", "screenshot")
	if err != nil {
		return nil, operator.NewInternalError("failed to create temp directory", err)
	}

	defer os.RemoveAll(tempdir)

	screenshotPath := filepath.Join(tempdir, "screenshot.png")

	if err := a.cvdCLI.DisplayScreenshot(a.selector, 0, screenshotPath); err != nil {
		return nil, operator.NewInternalError("cvd display screenshot failed", err)
	}

	screenshotBytes, err := ioutil.ReadFile(screenshotPath)
	if err != nil {
		return nil, operator.NewInternalError("failed to read screenshot file", err)
	}

	return &apiv1.DisplayScreenshotResponse{
		ScreenshotBytesBase64: base64.StdEncoding.EncodeToString(screenshotBytes),
		ScreenshotMimeType:    "image/png",
	}, nil
}
