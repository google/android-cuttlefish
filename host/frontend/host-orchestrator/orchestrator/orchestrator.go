// Copyright 2021 Google LLC
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
	"encoding/json"
	"fmt"
	"net/http"
	"os/exec"

	apiv1 "cuttlefish/liboperator/api/v1"
	"cuttlefish/liboperator/operator"

	"github.com/gorilla/mux"
)

func SetupRoutes(router *mux.Router, im *InstanceManager, om OperationManager) {
	// Instance Manager routes
	router.HandleFunc("/devices", func(w http.ResponseWriter, r *http.Request) {
		createDevices(w, r, im)
	}).Methods("POST")
	router.HandleFunc("/operations/{name}", func(w http.ResponseWriter, r *http.Request) {
		getOperation(w, r, om)
	}).Methods("GET")
	// _debug routes
	router.HandleFunc("/_debug/logs", func(w http.ResponseWriter, r *http.Request) {
		getServiceLogs(w, r)
	}).Methods("GET")
}

func createDevices(w http.ResponseWriter, r *http.Request, im *InstanceManager) {
	var msg apiv1.CreateCVDRequest
	err := json.NewDecoder(r.Body).Decode(&msg)
	if err != nil {
		operator.ReplyJSONErr(w, operator.NewBadRequestError("Malformed JSON in request", err))
		return
	}
	op, err := im.CreateCVD(msg)
	if err != nil {
		operator.ReplyJSONErr(w, err)
		return
	}
	operator.ReplyJSONOK(w, BuildOperation(op))
}

func getOperation(w http.ResponseWriter, r *http.Request, om OperationManager) {
	vars := mux.Vars(r)
	name := vars["name"]
	if op, err := om.Get(name); err != nil {
		operator.ReplyJSONErr(w, operator.NewNotFoundError("operation not found", err))
	} else {
		operator.ReplyJSONOK(w, BuildOperation(op))
	}
}

func BuildOperation(op Operation) apiv1.Operation {
	result := apiv1.Operation{
		Name: op.Name,
		Done: op.Done,
	}
	if !op.Done {
		return result
	}
	if op.IsError() {
		result.Result = &apiv1.OperationResult{
			Error: &apiv1.ErrorMsg{op.Result.Error.ErrorMsg},
		}
	}
	return result
}

func getServiceLogs(w http.ResponseWriter, _ *http.Request) {
	cmd := exec.Command("journalctl", "-u", "cuttlefish-host-orchestrator.service")
	stdoutStderr, err := cmd.CombinedOutput()
	if err != nil {
		fmt.Fprintf(w, "error fetching logs: %v\n", err)
		return
	}
	w.Write(stdoutStderr)
}
