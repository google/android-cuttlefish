package orchestrator

import (
	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd/output"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

func mapStatusToAPI(status *output.Status) []*apiv1.CVDInstanceStatus {
	var apiStatus []*apiv1.CVDInstanceStatus
	for _, s := range *status {
		apiStatus = append(apiStatus, &apiv1.CVDInstanceStatus{
			InstanceName:   s.InstanceName,
			Status:         s.Status,
			Displays:       s.Displays,
			AssemblyDir:    s.AssemblyDir,
			InstanceDir:    s.InstanceDir,
			WebRTCDeviceID: s.WebRTCDeviceID,
			ADBPort:        int(s.ADBPort),
			ADBSerial:      s.ADBSerial,
		})
	}
	return apiStatus
}

type CVDInstanceStatusActionOpts struct {
	Selector    cvd.InstanceSelector
	ExecContext exec.ExecContext
}

type CVDInstanceStatusAction struct {
	selector cvd.InstanceSelector
	cvdCLI   *cvd.CLI
}

func NewCVDInstanceStatusAction(opts CVDInstanceStatusActionOpts) *CVDInstanceStatusAction {
	return &CVDInstanceStatusAction{
		selector: opts.Selector,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func (a *CVDInstanceStatusAction) Run() (*apiv1.CVDStatusResponse, error) {
	status, err := a.cvdCLI.LazySelectInstance(a.selector).CVDStatus()
	if err != nil {
		return nil, operator.NewInternalError("cvd instance status failed", err)
	}
	return &apiv1.CVDStatusResponse{Status: mapStatusToAPI(status)}, nil
}
