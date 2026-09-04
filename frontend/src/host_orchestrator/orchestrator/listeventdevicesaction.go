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

type ListEventDevicesActionOpts struct {
	Selector    cvd.InstanceSelector
	ExecContext exec.ExecContext
}

type ListEventDevicesAction struct {
	selector cvd.InstanceSelector
	cvdCLI   *cvd.CLI
}

func NewListEventDevicesAction(opts ListEventDevicesActionOpts) *ListEventDevicesAction {
	return &ListEventDevicesAction{
		selector: opts.Selector,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func toApiv1ListEventDevicesResponse(devices []apiv1.EventDevice) *apiv1.ListEventDevicesResponse {
	response := apiv1.ListEventDevicesResponse{
		EventDevices: []apiv1.EventDevice{},
	}
	if devices != nil {
		response.EventDevices = append(response.EventDevices, devices...)
	}
	return &response
}

func (a *ListEventDevicesAction) Run() (*apiv1.ListEventDevicesResponse, error) {
	deviceNames, err := a.cvdCLI.LazySelectInstance(a.selector).ListEventDevices()
	if err != nil {
		return nil, operator.NewInternalError("failed to list input devices", err)
	}
	devices := []apiv1.EventDevice{}
	for _, n := range deviceNames {
		devices = append(devices, apiv1.EventDevice{
			Name: n,
		})
	}
	return toApiv1ListEventDevicesResponse(devices), nil
}
