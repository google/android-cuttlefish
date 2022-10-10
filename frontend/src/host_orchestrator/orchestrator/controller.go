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
	"net/http"
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/gorilla/mux"
)

type Controller struct {
	InstanceManager       InstanceManager
	OperationManager      OperationManager
	WaitOperationDuration time.Duration
}

func (c *Controller) AddRoutes(router *mux.Router) {
	router.Handle("/cvds", &createCVDHandler{im: c.InstanceManager}).Methods("POST")
	router.Handle("/operations/{name}", &getOperationHandler{om: c.OperationManager}).Methods("GET")
	// Waits for the specified Operation resource to be DONE or for the request to approach the
	// specified deadline, `503 Service Unavailable` error will be returned if the deadline is
	// reached. Be prepared to retry if the deadline was reached.
	router.Handle("/operations/{name}/wait",
		&waitOperationHandler{c.OperationManager, c.WaitOperationDuration}).Methods("POST")
}

type createCVDHandler struct {
	im InstanceManager
}

func (h *createCVDHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	var msg apiv1.CreateCVDRequest
	err := json.NewDecoder(r.Body).Decode(&msg)
	if err != nil {
		operator.ReplyJSONErr(w, operator.NewBadRequestError("Malformed JSON in request", err))
		return
	}
	op, err := h.im.CreateCVD(msg)
	if err != nil {
		operator.ReplyJSONErr(w, err)
		return
	}
	operator.ReplyJSONOK(w, BuildOperation(op))
}

type getOperationHandler struct {
	om OperationManager
}

func (h *getOperationHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	name := vars["name"]
	if op, err := h.om.Get(name); err != nil {
		operator.ReplyJSONErr(w, operator.NewNotFoundError("Operation not found", err))
	} else {
		operator.ReplyJSONOK(w, BuildOperation(op))
	}
}

type waitOperationHandler struct {
	om           OperationManager
	waitDuration time.Duration
}

func (h *waitOperationHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	name := vars["name"]
	op, err := h.om.Wait(name, h.waitDuration)
	if err == nil {
		operator.ReplyJSONOK(w, BuildOperation(op))
		return
	}
	if _, ok := err.(NotFoundOperationError); ok {
		operator.ReplyJSONErr(w, operator.NewNotFoundError("Operation not found", err))
		return
	}
	if _, ok := err.(*OperationWaitTimeoutError); ok {
		operator.ReplyJSONErr(w,
			operator.NewServiceUnavailableError("Wait for operation timed out", err))
		return
	}
	operator.ReplyJSONErr(w, operator.NewInternalError("Unknown wait operation error", err))
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
