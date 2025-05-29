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

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
)

type execCvdCommand interface {
	exec(cvd *cvd.CLI, sel cvd.Selector) error
}

type startCvdCommand struct{}

func (a *startCvdCommand) exec(cvdCLI *cvd.CLI, sel cvd.Selector) error {
	return cvdCLI.Start(sel, cvd.StartOptions{})
}

type stopCvdCommand struct{}

func (a *stopCvdCommand) exec(cvd *cvd.CLI, sel cvd.Selector) error {
	return cvd.Stop(sel)
}

type removeCvdCommand struct{}

func (a *removeCvdCommand) exec(cvd *cvd.CLI, sel cvd.Selector) error {
	return cvd.Remove(sel)
}

type powerwashCvdCommand struct{}

func (a *powerwashCvdCommand) exec(cvd *cvd.CLI, sel cvd.Selector) error {
	return cvd.PowerWash(sel)
}

type powerbtnCvdCommand struct{}

func (a *powerbtnCvdCommand) exec(cvd *cvd.CLI, sel cvd.Selector) error {
	return cvd.PowerBtn(sel)
}

type ExecCVDCommandActionOpts struct {
	Command          execCvdCommand
	Selector         cvd.Selector
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type ExecCVDCommandAction struct {
	command  execCvdCommand
	selector cvd.Selector
	paths    IMPaths
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewExecCVDCommandAction(opts ExecCVDCommandActionOpts) *ExecCVDCommandAction {
	return &ExecCVDCommandAction{
		command:  opts.Command,
		selector: opts.Selector,
		paths:    opts.Paths,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func (a *ExecCVDCommandAction) Run() (apiv1.Operation, error) {
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		result.Value = &apiv1.EmptyResponse{}
		result.Error = a.command.exec(a.cvdCLI, a.selector)
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op)
	return op, nil
}
