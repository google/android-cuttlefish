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
	"os/user"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type ExecCVDCommandActionOpts struct {
	Command          string
	Selector         CVDSelector
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      ExecContext
	CVDUser          *user.User
}

type ExecCVDCommandAction struct {
	command     string
	selector    CVDSelector
	paths       IMPaths
	om          OperationManager
	execContext cvd.CVDExecContext
}

func NewExecCVDCommandAction(opts ExecCVDCommandActionOpts) *ExecCVDCommandAction {
	return &ExecCVDCommandAction{
		command:     opts.Command,
		selector:    opts.Selector,
		paths:       opts.Paths,
		om:          opts.OperationManager,
		execContext: newCVDExecContext(opts.ExecContext, opts.CVDUser),
	}
}

func (a *ExecCVDCommandAction) Run() (apiv1.Operation, error) {
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

func (a *ExecCVDCommandAction) exec(op apiv1.Operation) (*apiv1.EmptyResponse, error) {
	args := a.selector.ToCVDCLI()
	args = append(args, a.command)
	cmd := cvd.NewCommand(a.execContext, args, cvd.CommandOpts{})
	if err := cmd.Run(); err != nil {
		return nil, operator.NewInternalError("cvd "+a.command+" failed", err)
	}
	return &apiv1.EmptyResponse{}, nil
}
