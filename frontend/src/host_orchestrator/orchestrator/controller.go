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
	"log"
	"mime/multipart"
	"net/http"
	"strconv"
	"time"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/debug"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/gorilla/mux"
)

type Controller struct {
	InstanceManager       InstanceManager
	OperationManager      OperationManager
	WaitOperationDuration time.Duration
	UserArtifactsManager  UserArtifactsManager
	DebugVariablesManager *debug.VariablesManager
}

func (c *Controller) AddRoutes(router *mux.Router) {
	router.Handle("/cvds", httpHandler(&createCVDHandler{im: c.InstanceManager})).Methods("POST")
	router.Handle("/cvds", httpHandler(&listCVDsHandler{im: c.InstanceManager})).Methods("GET")
	router.PathPrefix("/cvds/{name}/logs").Handler(&getCVDLogsHandler{im: c.InstanceManager}).Methods("GET")
	router.Handle("/operations/{name}", httpHandler(&getOperationHandler{om: c.OperationManager})).Methods("GET")
	// The expected response of the operation in case of success.  If the original method returns no data on
	// success, such as `Delete`, response will be empty. If the original method is standard
	// `Get`/`Create`/`Update`, the response should be the relevant resource encoded in JSON format.
	router.Handle("/operations/{name}/result",
		httpHandler(&getOperationResultHandler{om: c.OperationManager})).Methods("GET")
	// Same as `/operations/{name}/result but waits for the specified operation to be DONE or for the request
	// to approach the specified deadline, `503 Service Unavailable` error will be returned if the deadline is
	// reached. Be prepared to retry if the deadline was reached.
	router.Handle("/operations/{name}/:wait",
		httpHandler(&waitOperationHandler{c.OperationManager, c.WaitOperationDuration})).Methods("POST")
	router.Handle("/userartifacts",
		httpHandler(&createUploadDirectoryHandler{c.UserArtifactsManager})).Methods("POST")
	router.Handle("/userartifacts",
		httpHandler(&listUploadDirectoriesHandler{c.UserArtifactsManager})).Methods("GET")
	router.Handle("/userartifacts/{name}",
		httpHandler(&createUpdateUserArtifactHandler{c.UserArtifactsManager})).Methods("PUT")
	router.Handle("/varz", httpHandler(&getDebugVariablesHandler{c.DebugVariablesManager})).Methods("GET")
}

type handler interface {
	Handle(r *http.Request) (interface{}, error)
}

func httpHandler(h handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		res, err := h.Handle(r)
		if err != nil {
			log.Printf("request %q failed with error: %v", r.Method+" "+r.URL.Path, err)
			replyJSONErr(w, err)
			return
		}
		if res == nil {
			w.WriteHeader(http.StatusOK)
			return
		}
		replyJSONOK(w, res)
	})
}

func replyJSONOK(w http.ResponseWriter, obj interface{}) error {
	return replyJSON(w, obj, http.StatusOK)
}

func replyJSONErr(w http.ResponseWriter, err error) error {
	appErr, ok := err.(*operator.AppError)
	if !ok {
		return replyJSON(w, apiv1.ErrorMsg{Error: "Internal Server Error"}, http.StatusInternalServerError)
	}
	return replyJSON(w, appErr.JSONResponse(), appErr.StatusCode)
}

func replyJSON(w http.ResponseWriter, obj interface{}, statusCode int) error {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	encoder := json.NewEncoder(w)
	return encoder.Encode(obj)
}

type createCVDHandler struct {
	im InstanceManager
}

func (h *createCVDHandler) Handle(r *http.Request) (interface{}, error) {
	var msg apiv1.CreateCVDRequest
	err := json.NewDecoder(r.Body).Decode(&msg)
	if err != nil {
		return nil, operator.NewBadRequestError("Malformed JSON in request", err)
	}
	return h.im.CreateCVD(msg)
}

type listCVDsHandler struct {
	im InstanceManager
}

func (h *listCVDsHandler) Handle(r *http.Request) (interface{}, error) {
	return h.im.ListCVDs()
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

func (h *getOperationHandler) Handle(r *http.Request) (interface{}, error) {
	vars := mux.Vars(r)
	name := vars["name"]
	op, err := h.om.Get(name)
	if err != nil {
		var resErr error
		if _, ok := err.(NotFoundOperationError); ok {
			resErr = operator.NewNotFoundError("Operation not found", err)
		} else {
			resErr = operator.NewInternalError("Unexpected get operation error", err)
		}
		return nil, resErr
	}
	return op, nil
}

type getOperationResultHandler struct {
	om OperationManager
}

func (h *getOperationResultHandler) Handle(r *http.Request) (interface{}, error) {
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
		return nil, resErr
	}
	if res.Error != nil {
		return nil, res.Error
	}
	return res.Value, nil
}

type waitOperationHandler struct {
	om           OperationManager
	waitDuration time.Duration
}

func (h *waitOperationHandler) Handle(r *http.Request) (interface{}, error) {
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
		return nil, resErr
	}
	if res.Error != nil {
		return nil, res.Error
	}
	return res.Value, nil
}

type createUploadDirectoryHandler struct {
	m UserArtifactsManager
}

func (h *createUploadDirectoryHandler) Handle(r *http.Request) (interface{}, error) {
	return h.m.NewDir()
}

type listUploadDirectoriesHandler struct {
	m UserArtifactsManager
}

func (h *listUploadDirectoriesHandler) Handle(r *http.Request) (interface{}, error) {
	return h.m.ListDirs()
}

type createUpdateUserArtifactHandler struct {
	m UserArtifactsManager
}

func (h *createUpdateUserArtifactHandler) Handle(r *http.Request) (interface{}, error) {
	vars := mux.Vars(r)
	dir := vars["name"]
	if err := r.ParseMultipartForm(0); err != nil {
		if err == multipart.ErrMessageTooLarge {
			return nil, &operator.AppError{
				StatusCode: http.StatusInsufficientStorage,
				Err:        err,
			}
		}
		return nil, operator.NewBadRequestError("Invalid multipart form request", err)
	}
	defer r.MultipartForm.RemoveAll()
	chunkNumberRaw := r.FormValue("chunk_number")
	chunkTotalRaw := r.FormValue("chunk_total")
	chunkSizeBytesRaw := r.FormValue("chunk_size_bytes")
	f, fheader, err := r.FormFile("file")
	if err != nil {
		return nil, err
	}
	chunkNumber, err := strconv.Atoi(chunkNumberRaw)
	if err != nil {
		return nil, operator.NewBadRequestError(
			fmt.Sprintf("Invalid chunk_number value: %q", chunkNumberRaw), err)
	}
	chunkTotal, err := strconv.Atoi(chunkTotalRaw)
	if err != nil {
		return nil, operator.NewBadRequestError(
			fmt.Sprintf("Invalid chunk_total form field value: %q", chunkTotalRaw), err)
	}
	chunkSizeBytes, err := strconv.ParseInt(chunkSizeBytesRaw, 10, 64)
	if err != nil {
		return nil, operator.NewBadRequestError(
			fmt.Sprintf("Invalid chunk_size_bytes form field value: %q", chunkSizeBytesRaw), err)
	}
	chunk := UserArtifactChunk{
		Name:           fheader.Filename,
		ChunkNumber:    chunkNumber,
		ChunkTotal:     chunkTotal,
		ChunkSizeBytes: chunkSizeBytes,
		File:           f,
	}
	return nil, h.m.UpdateArtifact(dir, chunk)
}

type getDebugVariablesHandler struct {
	m *debug.VariablesManager
}

func (h *getDebugVariablesHandler) Handle(r *http.Request) (interface{}, error) {
	return h.m.GetVariables(), nil
}
