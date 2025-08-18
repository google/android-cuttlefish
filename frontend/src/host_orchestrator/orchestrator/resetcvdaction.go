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
	"log"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type ResetCVDActionOpts struct {
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type ResetCVDAction struct {
	om     OperationManager
	cvdCLI *cvd.CLI
}

func NewResetCVDAction(opts ResetCVDActionOpts) *ResetCVDAction {
	return &ResetCVDAction{
		om:     opts.OperationManager,
		cvdCLI: cvd.NewCLI(opts.ExecContext),
	}
}

func (a *ResetCVDAction) Run() (apiv1.Operation, error) {
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		if err := a.cvdCLI.Reset(); err != nil {
			result.Error = operator.NewInternalError("`cvd reset` failed: ", err)
		} else {
			result.Value = &apiv1.EmptyResponse{}
		}
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v", op.Name, err)
		}
	}(op)
	return op, nil
}
