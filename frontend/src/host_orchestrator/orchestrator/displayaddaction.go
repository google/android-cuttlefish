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

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd/output"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type DisplayAddActionOpts struct {
	Request          *apiv1.DisplayAddRequest
	Selector         cvd.InstanceSelector
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type DisplayAddAction struct {
	req      *apiv1.DisplayAddRequest
	selector cvd.InstanceSelector
	paths    IMPaths
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewDisplayAddAction(opts DisplayAddActionOpts) *DisplayAddAction {
	return &DisplayAddAction{
		req:      opts.Request,
		selector: opts.Selector,
		paths:    opts.Paths,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func toCvdDisplayAddOpts(req *apiv1.DisplayAddRequest) cvd.DisplayAddOpts {
	opts := cvd.DisplayAddOpts{
		Width:  req.Width,
		Height: req.Height,
	}
	if req.DPI != 0 {
		opts.DPI = req.DPI
	}
	if req.RefreshRateHZ != 0 {
		opts.RefreshRateHZ = req.RefreshRateHZ
	}
	return opts
}

func findNewDisplayNumber(oldDisplays *output.Displays, newDisplays *output.Displays) (int, error) {
	oldDisplaysLen := len(oldDisplays.Displays)
	newDisplaysLen := len(newDisplays.Displays)
	if newDisplaysLen-oldDisplaysLen != 1 {
		return -1, fmt.Errorf("display counts differ by more than 1: old:%d new:%d", oldDisplaysLen, newDisplaysLen)
	}

	for displayNumber := range newDisplays.Displays {
		_, found := oldDisplays.Displays[displayNumber]
		if !found {
			return displayNumber, nil
		}
	}
	return -1, fmt.Errorf("unreachable?")
}

func (a *DisplayAddAction) Run() (*apiv1.DisplayAddResponse, error) {
	instance := a.cvdCLI.LazySelectInstance(a.selector)
	oldDisplays, err := instance.ListDisplays()
	if err != nil {
		return nil, operator.NewInternalError("cvd display add: failed to list old displays:", err)
	}

	if err := instance.AddDisplay(toCvdDisplayAddOpts(a.req)); err != nil {
		return nil, operator.NewInternalError("cvd display add failed", err)
	}

	newDisplays, err := instance.ListDisplays()
	if err != nil {
		return nil, operator.NewInternalError("cvd display add: failed to list new displays:", err)
	}

	newDisplayNumber, err := findNewDisplayNumber(oldDisplays, newDisplays)
	if err != nil {
		return nil, operator.NewInternalError("cvd display add: failed to determine new display number:", err)
	}

	return &apiv1.DisplayAddResponse{
		DisplayNumber: newDisplayNumber,
	}, nil
}
