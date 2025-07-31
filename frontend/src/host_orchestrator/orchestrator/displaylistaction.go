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
	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type DisplayListActionOpts struct {
	Selector         cvd.Selector
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type DisplayListAction struct {
	selector cvd.Selector
	paths    IMPaths
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewDisplayListAction(opts DisplayListActionOpts) *DisplayListAction {
	return &DisplayListAction{
		selector: opts.Selector,
		paths:    opts.Paths,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func toApiv1DisplayMode(m cvd.DisplayMode) apiv1.DisplayMode {
	return apiv1.DisplayMode{
		Windowed: m.Windowed,
	}
}

func toApiv1Display(d *cvd.Display) apiv1.Display {
	return apiv1.Display{
		DPI:           d.DPI,
		RefreshRateHZ: d.RefreshRateHZ,
		Mode:          toApiv1DisplayMode(d.Mode),
	}
}

func toApiv1DisplayResponse(d *cvd.Displays) *apiv1.DisplayListResponse {
	resp := &apiv1.DisplayListResponse{
		Displays: make(map[int]apiv1.Display),
	}
	for displayNum, display := range d.Displays {
		resp.Displays[displayNum] = toApiv1Display(display)
	}
	return resp
}

func (a *DisplayListAction) Run() (*apiv1.DisplayListResponse, error) {
	displays, err := a.cvdCLI.DisplayList(a.selector)
	if err != nil {
		return nil, operator.NewInternalError("cvd display list failed", err)
	}
	return toApiv1DisplayResponse(displays), nil
}
