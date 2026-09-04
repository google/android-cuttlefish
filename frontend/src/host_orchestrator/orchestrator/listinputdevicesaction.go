// Copyright 2026 Google LLC
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

type ListInputDevicesActionOpts struct {
	Selector    cvd.InstanceSelector
	ExecContext exec.ExecContext
}

type ListInputDevicesAction struct {
	selector cvd.InstanceSelector
	cvdCLI   *cvd.CLI
}

func NewListInputDevicesAction(opts ListInputDevicesActionOpts) *ListInputDevicesAction {
	return &ListInputDevicesAction{
		selector: opts.Selector,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func toApiv1ListInputDevicesResponse(devices []string) *apiv1.ListInputDevicesResponse {
	response := apiv1.ListInputDevicesResponse{
		InputDevices: []string{},
	}
	if devices != nil {
		response.InputDevices = append(response.InputDevices, devices...)
	}
	return &response
}

func (a *ListInputDevicesAction) Run() (*apiv1.ListInputDevicesResponse, error) {
	devices, err := a.cvdCLI.LazySelectInstance(a.selector).ListInputDevices()
	if err != nil {
		return nil, operator.NewInternalError("failed to list input devices", err)
	}
	return toApiv1ListInputDevicesResponse(devices), nil
}
