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
	"log"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type StopCVDActionOpts struct {
	Selector         CVDSelector
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      ExecContext
	CVDUser          string
}

type StopCVDAction struct {
	selector    CVDSelector
	paths       IMPaths
	om          OperationManager
	execContext cvd.CVDExecContext
}

func NewStopCVDAction(opts StopCVDActionOpts) *StopCVDAction {
	return &StopCVDAction{
		selector:    opts.Selector,
		paths:       opts.Paths,
		om:          opts.OperationManager,
		execContext: newCVDExecContext(opts.ExecContext, opts.CVDUser),
	}
}

func (a *StopCVDAction) Run() (apiv1.Operation, error) {
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		result.Value, result.Error = a.exec(op)
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op)
	return op, nil
}

func (a *StopCVDAction) exec(op apiv1.Operation) (*apiv1.StopCVDResponse, error) {
	args := a.selector.ToCVDCLI()
	args = append(args, "stop")
	cmd := cvd.NewCommand(a.execContext, args, cvd.CommandOpts{})
	if err := cmd.Run(); err != nil {
		return nil, operator.NewInternalError("cvd stop failed", err)
	}
	return &apiv1.StopCVDResponse{}, nil
}
