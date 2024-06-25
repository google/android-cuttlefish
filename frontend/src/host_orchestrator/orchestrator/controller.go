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
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"sync/atomic"
	"time"
	"unsafe"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/artifacts"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/debug"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/google/uuid"
	"github.com/gorilla/mux"
)

const HeaderBuildAPICreds = "X-Cutf-Host-Orchestrator-BuildAPI-Creds"

type Config struct {
	Paths                  IMPaths
	AndroidBuildServiceURL string
	CVDUser                string
}

type ControllerOpts struct {
	Config                Config
	OperationManager      OperationManager
	WaitOperationDuration time.Duration
	UserArtifactsManager  UserArtifactsManager
	DebugVariablesManager *debug.VariablesManager
}

type Controller struct {
	config         Config
	om             OperationManager
	waitOpDuration time.Duration
	uam            UserArtifactsManager
	dvm            *debug.VariablesManager

	firstCreateCVDOpNamePtr *unsafe.Pointer
}

func NewController(opts ControllerOpts) *Controller {
	result := &Controller{
		config:         opts.Config,
		om:             opts.OperationManager,
		waitOpDuration: opts.WaitOperationDuration,
		uam:            opts.UserArtifactsManager,
		dvm:            opts.DebugVariablesManager,
	}

	var p unsafe.Pointer
	result.firstCreateCVDOpNamePtr = &p

	return result
}

func (c *Controller) AddRoutes(router *mux.Router) {
	router.Handle("/artifacts", httpHandler(&fetchArtifactsHandler{Config: c.config, OM: c.om})).Methods("POST")
	router.Handle("/cvds", httpHandler(newCreateCVDHandler(c.config, c.om, c.uam, c.firstCreateCVDOpNamePtr))).Methods("POST")
	router.Handle("/cvds", httpHandler(&listCVDsHandler{Config: c.config})).Methods("GET")
	router.PathPrefix("/cvds/{name}/logs").Handler(&getCVDLogsHandler{Config: c.config}).Methods("GET")
	router.Handle("/cvds/{group}", httpHandler(newExecCVDCommandHandler(c.config, c.om, "stop"))).Methods("DELETE")
	router.Handle("/cvds/{group}/{name}",
		httpHandler(newExecCVDCommandHandler(c.config, c.om, "stop"))).Methods("DELETE")
	router.Handle("/cvds/{group}/{name}/:powerwash",
		httpHandler(newExecCVDCommandHandler(c.config, c.om, "powerwash"))).Methods("POST")
	// Operation name: `firstcvdcreate` is an alias for the first operation tracking a cvd create action.
	router.Handle("/operations/{name}",
		httpHandler(&getOperationHandler{c.om, c.firstCreateCVDOpNamePtr})).Methods("GET")
	// The expected response of the operation in case of success.  If the original method returns no data on
	// success, such as `Delete`, response will be empty. If the original method is standard
	// `Get`/`Create`/`Update`, the response should be the relevant resource encoded in JSON format.
	router.Handle("/operations/{name}/result",
		httpHandler(&getOperationResultHandler{c.om, c.firstCreateCVDOpNamePtr})).Methods("GET")
	// Same as `/operations/{name}/result but waits for the specified operation to be DONE or for the request
	// to approach the specified deadline, `503 Service Unavailable` error will be returned if the deadline is
	// reached. Be prepared to retry if the deadline was reached.
	router.Handle("/operations/{name}/:wait",
		httpHandler(&waitOperationHandler{c.om, c.waitOpDuration, c.firstCreateCVDOpNamePtr})).Methods("POST")
	router.Handle("/userartifacts", httpHandler(&createUploadDirectoryHandler{c.uam})).Methods("POST")
	router.Handle("/userartifacts", httpHandler(&listUploadDirectoriesHandler{c.uam})).Methods("GET")
	router.Handle("/userartifacts/{name}", httpHandler(&createUpdateUserArtifactHandler{c.uam})).Methods("PUT")
	router.Handle("/userartifacts/{dir}/{name}/:extract",
		httpHandler(&extractUserArtifactHandler{c.om, c.uam})).Methods("POST")
	router.Handle("/runtimeartifacts/:pull", &pullRuntimeArtifactsHandler{Config: c.config}).Methods("POST")
	// Debug endpoints.
	router.Handle("/_debug/varz", httpHandler(&getDebugVariablesHandler{c.dvm})).Methods("GET")
	router.Handle("/_debug/statusz", okHandler()).Methods("GET")
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
		appErr, _ = (operator.NewInternalError("Internal server error", err)).(*operator.AppError)
	}
	return replyJSON(w, appErr.JSONResponse(), appErr.StatusCode)
}

func replyJSON(w http.ResponseWriter, obj interface{}, statusCode int) error {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	encoder := json.NewEncoder(w)
	return encoder.Encode(obj)
}

type fetchArtifactsHandler struct {
	Config Config
	OM     OperationManager
}

func (h *fetchArtifactsHandler) Handle(r *http.Request) (interface{}, error) {
	req := apiv1.FetchArtifactsRequest{}
	err := json.NewDecoder(r.Body).Decode(&req)
	if err != nil {
		return nil, operator.NewBadRequestError("Malformed JSON in request", err)
	}
	creds := r.Header.Get(HeaderBuildAPICreds)
	buildAPIOpts := artifacts.AndroidCIBuildAPIOpts{Credentials: creds}
	buildAPI := artifacts.NewAndroidCIBuildAPIWithOpts(
		http.DefaultClient, h.Config.AndroidBuildServiceURL, buildAPIOpts)
	artifactsFetcher := newBuildAPIArtifactsFetcher(buildAPI)
	execCtx := newCVDExecContext(exec.CommandContext, h.Config.CVDUser)
	cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(execCtx, creds)
	opts := FetchArtifactsActionOpts{
		Request:          &req,
		Paths:            h.Config.Paths,
		OperationManager: h.OM,
		BuildAPI:         buildAPI,
		CVDBundleFetcher: cvdBundleFetcher,
		ArtifactsFetcher: artifactsFetcher,
	}
	return NewFetchArtifactsAction(opts).Run()
}

type createCVDHandler struct {
	Config         Config
	OM             OperationManager
	UADirResolver  UserArtifactsDirResolver
	firstOpNamePtr *unsafe.Pointer
}

func newCreateCVDHandler(
	c Config,
	om OperationManager,
	uadr UserArtifactsManager,
	firstOpNamePtr *unsafe.Pointer) *createCVDHandler {
	return &createCVDHandler{
		Config:         c,
		OM:             om,
		UADirResolver:  uadr,
		firstOpNamePtr: firstOpNamePtr,
	}
}

func (h *createCVDHandler) Handle(r *http.Request) (interface{}, error) {
	req := &apiv1.CreateCVDRequest{}
	err := json.NewDecoder(r.Body).Decode(req)
	if err != nil {
		return nil, operator.NewBadRequestError("Malformed JSON in request", err)
	}
	creds := r.Header.Get(HeaderBuildAPICreds)
	buildAPIOpts := artifacts.AndroidCIBuildAPIOpts{Credentials: creds}
	buildAPI := artifacts.NewAndroidCIBuildAPIWithOpts(
		http.DefaultClient, h.Config.AndroidBuildServiceURL, buildAPIOpts)
	artifactsFetcher := newBuildAPIArtifactsFetcher(buildAPI)
	execCtx := newCVDExecContext(exec.CommandContext, h.Config.CVDUser)
	cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(execCtx, creds)
	opts := CreateCVDActionOpts{
		Request:                  req,
		HostValidator:            &HostValidator{ExecContext: exec.CommandContext},
		Paths:                    h.Config.Paths,
		OperationManager:         h.OM,
		ExecContext:              exec.CommandContext,
		BuildAPI:                 buildAPI,
		ArtifactsFetcher:         artifactsFetcher,
		CVDBundleFetcher:         cvdBundleFetcher,
		UUIDGen:                  func() string { return uuid.New().String() },
		CVDUser:                  h.Config.CVDUser,
		UserArtifactsDirResolver: h.UADirResolver,
		BuildAPICredentials:      creds,
	}
	op, err := NewCreateCVDAction(opts).Run()
	if err != nil {
		return nil, err
	}
	atomic.CompareAndSwapPointer(h.firstOpNamePtr, nil, unsafe.Pointer(&op.Name))
	return op, nil
}

type listCVDsHandler struct {
	Config Config
}

func (h *listCVDsHandler) Handle(r *http.Request) (interface{}, error) {
	opts := ListCVDsActionOpts{
		Paths:       h.Config.Paths,
		ExecContext: exec.CommandContext,
		CVDUser:     h.Config.CVDUser,
	}
	return NewListCVDsAction(opts).Run()
}

type execCVDCommandHandler struct {
	Config  Config
	OM      OperationManager
	Command string
}

func newExecCVDCommandHandler(c Config, om OperationManager, command string) *execCVDCommandHandler {
	return &execCVDCommandHandler{
		Config:  c,
		OM:      om,
		Command: command,
	}
}

func (h *execCVDCommandHandler) Handle(r *http.Request) (interface{}, error) {
	vars := mux.Vars(r)
	group := vars["group"]
	name := vars["name"]
	opts := ExecCVDCommandActionOpts{
		Command:          h.Command,
		Selector:         CVDSelector{Group: group, Name: name},
		Paths:            h.Config.Paths,
		OperationManager: h.OM,
		ExecContext:      exec.CommandContext,
		CVDUser:          h.Config.CVDUser,
	}
	return NewExecCVDCommandAction(opts).Run()
}

type getCVDLogsHandler struct {
	Config Config
}

func (h *getCVDLogsHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	name := vars["name"]
	pathPrefix := "/cvds/" + name + "/logs"
	ctx := newCVDExecContext(exec.CommandContext, h.Config.CVDUser)
	logsDir, err := CVDLogsDir(ctx, name)
	if err != nil {
		log.Printf("request %q failed with error: %v", r.Method+" "+r.URL.Path, err)
		appErr, ok := err.(*operator.AppError)
		if ok {
			w.WriteHeader(appErr.StatusCode)
		} else {
			w.WriteHeader(http.StatusInternalServerError)
		}
		return
	}
	handler := http.StripPrefix(pathPrefix, http.FileServer(http.Dir(logsDir)))
	handler.ServeHTTP(w, r)
}

const firstCVDCreateOpAlias = "firstcvdcreate"

func getOperation(r *http.Request, om OperationManager, firstNamePtr *unsafe.Pointer) (apiv1.Operation, error) {
	vars := mux.Vars(r)
	name := vars["name"]
	if name == firstCVDCreateOpAlias {
		if *firstNamePtr == nil {
			return apiv1.Operation{}, operator.NewNotFoundError("First create cvd operation not found", nil)
		}
		name = *(*string)(atomic.LoadPointer(firstNamePtr))
	}
	return om.Get(name)
}

type getOperationHandler struct {
	om           OperationManager
	firstNamePtr *unsafe.Pointer
}

func (h *getOperationHandler) Handle(r *http.Request) (interface{}, error) {
	op, err := getOperation(r, h.om, h.firstNamePtr)
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
	om           OperationManager
	firstNamePtr *unsafe.Pointer
}

func (h *getOperationResultHandler) Handle(r *http.Request) (interface{}, error) {
	op, err := getOperation(r, h.om, h.firstNamePtr)
	res, err := h.om.GetResult(op.Name)
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
	firstNamePtr *unsafe.Pointer
}

func (h *waitOperationHandler) Handle(r *http.Request) (interface{}, error) {
	op, err := getOperation(r, h.om, h.firstNamePtr)
	res, err := h.om.Wait(op.Name, h.waitDuration)
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

type extractUserArtifactHandler struct {
	om  OperationManager
	uam UserArtifactsManager
}

func (h *extractUserArtifactHandler) Handle(r *http.Request) (interface{}, error) {
	vars := mux.Vars(r)
	dir := vars["dir"]
	name := vars["name"]
	op := h.om.New()
	go func() {
		err := h.uam.ExtractArtifact(dir, name)
		if err := h.om.Complete(op.Name, &OperationResult{Error: err}); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}()
	return op, nil
}

type pullRuntimeArtifactsHandler struct {
	Config Config
}

func (h *pullRuntimeArtifactsHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	filename := filepath.Join("/tmp/", "hostbugreport"+uuid.New().String())
	defer func() {
		if err := os.Remove(filename); err != nil {
			log.Printf("error removing host bug report file %q: %v\n", filename, err)
		}
	}()
	ctx := newCVDExecContext(exec.CommandContext, h.Config.CVDUser)
	if err := HostBugReport(ctx, h.Config.Paths, filename); err != nil {
		replyJSONErr(w, err)
		return
	}
	http.ServeFile(w, r, filename)
}

type getDebugVariablesHandler struct {
	m *debug.VariablesManager
}

func (h *getDebugVariablesHandler) Handle(r *http.Request) (interface{}, error) {
	return h.m.GetVariables(), nil
}

func okHandler() http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
	})
}
