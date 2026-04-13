package orchestrator

import (
	"log"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type CVDGroupStatusActionOpts struct {
	Selector         cvd.GroupSelector
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type CVDGroupStatusAction struct {
	selector cvd.GroupSelector
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewCVDGroupStatusAction(opts CVDGroupStatusActionOpts) *CVDGroupStatusAction {
	return &CVDGroupStatusAction{
		selector: opts.Selector,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func (a *CVDGroupStatusAction) Run() (apiv1.Operation, error) {
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		status, err := a.cvdCLI.LazySelectGroup(a.selector).CVDStatus()
		if err == nil {
			result.Value = &apiv1.CVDStatusResponse{Status: status}
		} else {
			result.Error = operator.NewInternalError("cvd group status failed", err)
		}
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op)
	return op, nil
}

type CVDInstanceStatusActionOpts struct {
	Selector         cvd.InstanceSelector
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type CVDInstanceStatusAction struct {
	selector cvd.InstanceSelector
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewCVDInstanceStatusAction(opts CVDInstanceStatusActionOpts) *CVDInstanceStatusAction {
	return &CVDInstanceStatusAction{
		selector: opts.Selector,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func (a *CVDInstanceStatusAction) Run() (apiv1.Operation, error) {
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		status, err := a.cvdCLI.LazySelectInstance(a.selector).CVDStatus()
		if err == nil {
			result.Value = &apiv1.CVDStatusResponse{Status: status}
		} else {
			result.Error = operator.NewInternalError("cvd instance status failed", err)
		}
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op)
	return op, nil
}
