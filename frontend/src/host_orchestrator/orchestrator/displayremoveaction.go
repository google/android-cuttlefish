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

type DisplayRemoveActionOpts struct {
	DisplayNumber    int
	Selector         cvd.Selector
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type DisplayRemoveAction struct {
	displayNumber int
	selector      cvd.Selector
	paths         IMPaths
	om            OperationManager
	cvdCLI        *cvd.CLI
}

func NewDisplayRemoveAction(opts DisplayRemoveActionOpts) *DisplayRemoveAction {
	return &DisplayRemoveAction{
		displayNumber: opts.DisplayNumber,
		selector:      opts.Selector,
		paths:         opts.Paths,
		om:            opts.OperationManager,
		cvdCLI:        cvd.NewCLI(opts.ExecContext),
	}
}

func (a *DisplayRemoveAction) Run() (*apiv1.DisplayRemoveResponse, error) {
	if err := a.cvdCLI.DisplayRemove(a.selector, a.displayNumber); err != nil {
		return nil, operator.NewInternalError("cvd display remove failed:", err)
	}

	return &apiv1.DisplayRemoveResponse{}, nil
}
