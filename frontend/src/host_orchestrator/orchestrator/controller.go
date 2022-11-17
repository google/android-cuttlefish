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
	router.PathPrefix("/cvds/{name}/logs").Handler(&getCVDLogsHandler{im: c.InstanceManager}).Methods("GET")
	router.Handle("/operations/{name}", &getOperationHandler{om: c.OperationManager}).Methods("GET")
	// The expected response of the operation in case of success.  If the original method returns no data on
	// success, such as `Delete`, response will be empty. If the original method is standard
	// `Get`/`Create`/`Update`, the response should be the relevant resource encoded in JSON format.
	router.Handle("/operations/{name}/result", &getOperationResultHandler{om: c.OperationManager}).Methods("GET")
	// Same as `/operations/{name}/result but waits for the specified operation to be DONE or for the request
	// to approach the specified deadline, `503 Service Unavailable` error will be returned if the deadline is
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
	operator.ReplyJSONOK(w, op)
}

type getCVDLogsHandler struct {
	im InstanceManager
}

func (h *getCVDLogsHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	name := vars["name"]
	pathPrefix := "/cvds/" + name + "/logs"
	logsDir := h.im.GetLogsDir(name)
	handler := http.StripPrefix(pathPrefix, http.FileServer(http.Dir(logsDir)))
	handler.ServeHTTP(w, r)
}

type getOperationHandler struct {
	om OperationManager
}

func (h *getOperationHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	name := vars["name"]
	if op, err := h.om.Get(name); err != nil {
		var resErr error
		if _, ok := err.(NotFoundOperationError); ok {
			resErr = operator.NewNotFoundError("Operation not found", err)
		} else {
			resErr = operator.NewInternalError("Unexpected get operation error", err)
		}
		operator.ReplyJSONErr(w, resErr)

	} else {
		operator.ReplyJSONOK(w, op)
	}
}

type getOperationResultHandler struct {
	om OperationManager
}

func (h *getOperationResultHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	name := vars["name"]
	res, err := h.om.GetResult(name)
	if err != nil {
		var resErr error
		if _, ok := err.(NotFoundOperationError); ok {
			resErr = operator.NewNotFoundError("Operation not found", err)
		} else if _, ok := err.(OperationNotDoneError); ok {
			resErr = operator.NewNotFoundError("Operation not done", err)
		} else {
			resErr = operator.NewInternalError("Unexpected get operation error", err)
		}
		operator.ReplyJSONErr(w, resErr)
		return
	}
	if res.Error != nil {
		operator.ReplyJSONErr(w, res.Error)
		return
	}
	operator.ReplyJSONOK(w, res.Value)
}

type waitOperationHandler struct {
	om           OperationManager
	waitDuration time.Duration
}

func (h *waitOperationHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	name := vars["name"]
	res, err := h.om.Wait(name, h.waitDuration)
	if err != nil {
		var resErr error
		if _, ok := err.(NotFoundOperationError); ok {
			resErr = operator.NewNotFoundError("Operation not found", err)
		} else if _, ok := err.(*OperationWaitTimeoutError); ok {
			resErr = operator.NewServiceUnavailableError("Wait for operation timed out", err)
		} else {
			resErr = operator.NewInternalError("Unexpected get operation error", err)
		}
		operator.ReplyJSONErr(w, resErr)
		return
	}
	if res.Error != nil {
		operator.ReplyJSONErr(w, res.Error)
		return
	}
	operator.ReplyJSONOK(w, res.Value)

}
