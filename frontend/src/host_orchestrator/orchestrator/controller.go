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
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/debug"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/google/uuid"
	"github.com/gorilla/mux"
)

const HeaderBuildAPICreds = "X-Cutf-Host-Orchestrator-BuildAPI-Creds"
const HeaderUserProject = "X-Cutf-Host-Orchestrator-BuildAPI-Creds-User-Project-ID"

const (
	URLQueryKeyIncludeAdbBugReport = "include_adb_bugreport"
)

type Config struct {
	Paths                  IMPaths
	AndroidBuildServiceURL string
}

type Controller struct {
	Config                Config
	OperationManager      OperationManager
	WaitOperationDuration time.Duration
	UserArtifactsManager  UserArtifactsManager
	DebugVariablesManager *debug.VariablesManager
}

func (c *Controller) AddRoutes(router *mux.Router) {
	router.Handle("/artifacts",
		httpHandler(&fetchArtifactsHandler{Config: c.Config, OM: c.OperationManager})).Methods("POST")
	router.Handle("/cvds",
		httpHandler(newCreateCVDHandler(c.Config, c.OperationManager, c.UserArtifactsManager))).Methods("POST")
	router.Handle("/cvds", httpHandler(&listCVDsHandlerAll{Config: c.Config})).Methods("GET")
	router.Handle("/cvds/{group}", httpHandler(&listCVDsHandler{Config: c.Config})).Methods("GET")
	router.PathPrefix("/cvds/{group}/{name}/logs").Handler(&getCVDLogsHandler{Config: c.Config}).Methods("GET")
	router.Handle("/cvds/{group}/:start",
		httpHandler(newExecCVDCommandHandler(c.Config, c.OperationManager, &startCvdCommand{}))).Methods("POST")
	router.Handle("/cvds/{group}/:stop",
		httpHandler(newExecCVDCommandHandler(c.Config, c.OperationManager, &stopCvdCommand{}))).Methods("POST")
	router.Handle("/cvds/{group}",
		httpHandler(newExecCVDCommandHandler(c.Config, c.OperationManager, &removeCvdCommand{}))).Methods("DELETE")
	// Append `include_adb_bugreport=true` query parameter to include a device `adb bugreport` in the cvd bugreport.
	router.Handle("/cvds/{group}/:bugreport",
		httpHandler(newCreateCVDBugReportHandler(c.Config, c.OperationManager))).Methods("POST")
	router.Handle("/cvds/{group}/{name}",
		httpHandler(newExecCVDCommandHandler(c.Config, c.OperationManager, &removeCvdCommand{}))).Methods("DELETE")
	router.Handle("/cvds/{group}/{name}/:start",
		httpHandler(newStartCVDHandler(c.Config, c.OperationManager))).Methods("POST")
	router.Handle("/cvds/{group}/{name}/:stop",
		httpHandler(newExecCVDCommandHandler(c.Config, c.OperationManager, &stopCvdCommand{}))).Methods("POST")
	router.Handle("/cvds/{group}/{name}/:powerwash",
		httpHandler(newExecCVDCommandHandler(c.Config, c.OperationManager, &powerwashCvdCommand{}))).Methods("POST")
	router.Handle("/cvds/{group}/{name}/:powerbtn",
		httpHandler(newExecCVDCommandHandler(c.Config, c.OperationManager, &powerbtnCvdCommand{}))).Methods("POST")
	router.Handle("/cvds/{group}/{name}/snapshots",
		httpHandler(newCreateSnapshotHandler(c.Config, c.OperationManager))).Methods("POST")
	router.Handle("/operations", httpHandler(&listOperationsHandler{om: c.OperationManager})).Methods("GET")
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
	router.Handle("/cvdbugreports/{uuid}", &downloadCVDBugReportHandler{c.Config}).Methods("GET")
	router.Handle("/cvdbugreports/{uuid}", httpHandler(&deleteCVDBugReportHandler{c.Config})).Methods("DELETE")
	router.Handle("/snapshots/{id}", httpHandler(&deleteSnapshotHandler{c.Config, c.OperationManager})).Methods("DELETE")
	router.Handle("/userartifacts",
		httpHandler(&createUploadDirectoryHandler{c.UserArtifactsManager})).Methods("POST")
	router.Handle("/userartifacts",
		httpHandler(&listUploadDirectoriesHandler{c.UserArtifactsManager})).Methods("GET")
	router.Handle("/userartifacts/{name}",
		httpHandler(&createUpdateUserArtifactHandler{c.UserArtifactsManager, false})).Methods("PUT")
	router.Handle("/userartifacts/{dir}/{name}/:extract",
		httpHandler(&extractUserArtifactHandler{c.OperationManager, c.UserArtifactsManager, false})).Methods("POST")
	router.Handle("/v1/userartifacts/{checksum}",
		httpHandler(&createUpdateUserArtifactHandler{c.UserArtifactsManager, true})).Methods("PUT")
	router.Handle("/v1/userartifacts/{checksum}",
		httpHandler(&statUserArtifactHandler{c.UserArtifactsManager})).Methods("GET")
	router.Handle("/v1/userartifacts/{checksum}/:extract",
		httpHandler(&extractUserArtifactHandler{c.OperationManager, c.UserArtifactsManager, true})).Methods("POST")
	// Debug endpoints.
	router.Handle("/_debug/varz", httpHandler(&getDebugVariablesHandler{c.DebugVariablesManager})).Methods("GET")
	router.Handle("/_debug/statusz", okHandler()).Methods("GET")
}

type handler interface {
	Handle(r *http.Request) (interface{}, error)
}

func httpHandler(h handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		log.Printf("handling request(%p) started %s %s", r, r.Method, r.URL.Path)
		defer log.Printf("handling request(%p) ended", r)

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

// FetchArtifacts godoc
//
//	@Summary		Fetches and stores artifacts from the Android Build API
//	@Description	Fetches and stores artifacts from the Android Build API
//	@Accept			json
//	@Produce		json
//	@Param			FetchArtifactsRequest					body		apiv1.FetchArtifactsRequest	true	" "
//	@Param			X-Cutf-Host-Orchestrator-BuildAPI-Creds	header		string						false	"Use this header for forwarding EUC towards the Android Build API"
//	@Success		200										{object}	apiv1.Operation
//	@Router			/artifacts [post]
func (h *fetchArtifactsHandler) Handle(r *http.Request) (interface{}, error) {
	req := apiv1.FetchArtifactsRequest{}
	err := json.NewDecoder(r.Body).Decode(&req)
	if err != nil {
		return nil, operator.NewBadRequestError("Malformed JSON in request", err)
	}
	creds := getFetchCredentials(r)
	cvdBundleFetcher :=
		newFetchCVDCommandArtifactsFetcher(exec.CommandContext, creds, h.Config.AndroidBuildServiceURL)
	opts := FetchArtifactsActionOpts{
		Request:          &req,
		Paths:            h.Config.Paths,
		OperationManager: h.OM,
		CVDBundleFetcher: cvdBundleFetcher,
	}
	return NewFetchArtifactsAction(opts).Run()
}

type createCVDHandler struct {
	Config        Config
	OM            OperationManager
	UADirResolver UserArtifactsDirResolver
}

func newCreateCVDHandler(c Config, om OperationManager, uadr UserArtifactsManager) *createCVDHandler {
	return &createCVDHandler{
		Config:        c,
		OM:            om,
		UADirResolver: uadr,
	}
}

// CreateCVD godoc
//
//	@Summary		Creates cuttlefish instances
//	@Description	Creates cuttlefish instances
//	@Accept			json
//	@Produce		json
//	@Param			CreateCVDRequest						body		apiv1.CreateCVDRequest	true	"CreateCVDRequest object instance"
//	@Param			X-Cutf-Host-Orchestrator-BuildAPI-Creds	header		string					false	"Use this header for forwarding EUC towards the Android Build API"
//	@Success		200										{object}	apiv1.CreateCVDResponse
//	@Router			/cvds [post]
func (h *createCVDHandler) Handle(r *http.Request) (interface{}, error) {
	req := &apiv1.CreateCVDRequest{}
	err := json.NewDecoder(r.Body).Decode(req)
	if err != nil {
		return nil, operator.NewBadRequestError("Malformed JSON in request", err)
	}
	creds := getFetchCredentials(r)
	cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(exec.CommandContext, creds, h.Config.AndroidBuildServiceURL)
	opts := CreateCVDActionOpts{
		Request:                  req,
		HostValidator:            &HostValidator{ExecContext: exec.CommandContext},
		Paths:                    h.Config.Paths,
		OperationManager:         h.OM,
		ExecContext:              exec.CommandContext,
		CVDBundleFetcher:         cvdBundleFetcher,
		UUIDGen:                  func() string { return uuid.New().String() },
		UserArtifactsDirResolver: h.UADirResolver,
		FetchCredentials:         creds,
		BuildAPIBaseURL:          h.Config.AndroidBuildServiceURL,
	}
	return NewCreateCVDAction(opts).Run()
}

type listCVDsHandlerAll struct {
	Config Config
}

// ListCVDs all godoc
//
//	@Summary		List all cuttlefish instances
//	@Description	List all cuttlefish instances
//	@Produce		json
//	@Success		200	{object}	apiv1.ListCVDsResponse
//	@Router			/cvds [get]
func (h *listCVDsHandlerAll) Handle(r *http.Request) (interface{}, error) {
	opts := ListCVDsActionOpts{
		Paths:       h.Config.Paths,
		ExecContext: exec.CommandContext,
	}
	return NewListCVDsAction(opts).Run()
}

type listCVDsHandler struct {
	Config Config
}

// ListCVDs by group godoc
//
//	@Summary		List cuttlefish instances in a group
//	@Description	List cuttlefish instances in a group
//	@Produce		json
//	@Param			group	path		string	true	"Group Name"
//	@Success		200		{object}	apiv1.ListCVDsResponse
//	@Router			/cvds/{group} [get]
func (h *listCVDsHandler) Handle(r *http.Request) (interface{}, error) {
	vars := mux.Vars(r)
	opts := ListCVDsActionOpts{
		Group:       vars["group"],
		Paths:       h.Config.Paths,
		ExecContext: exec.CommandContext,
	}
	return NewListCVDsAction(opts).Run()
}

type execCVDCommandHandler struct {
	Config  Config
	OM      OperationManager
	Command execCvdCommand
}

func newExecCVDCommandHandler(c Config, om OperationManager, command execCvdCommand) *execCVDCommandHandler {
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
		Selector:         cvd.Selector{Group: group, Instance: name},
		Paths:            h.Config.Paths,
		OperationManager: h.OM,
		ExecContext:      exec.CommandContext,
	}
	return NewExecCVDCommandAction(opts).Run()
}

type createSnapshotHandler struct {
	Config Config
	OM     OperationManager
}

func newCreateSnapshotHandler(c Config, om OperationManager) *createSnapshotHandler {
	return &createSnapshotHandler{Config: c, OM: om}
}

func (h *createSnapshotHandler) Handle(r *http.Request) (interface{}, error) {
	req := &apiv1.CreateSnapshotRequest{}
	err := json.NewDecoder(r.Body).Decode(req)
	if err != nil {
		return nil, operator.NewBadRequestError("malformed JSON in request", err)
	}
	vars := mux.Vars(r)
	group := vars["group"]
	name := vars["name"]
	opts := CreateSnapshotActionOpts{
		Request:          req,
		Selector:         cvd.Selector{Group: group, Instance: name},
		Paths:            h.Config.Paths,
		OperationManager: h.OM,
		ExecContext:      exec.CommandContext,
	}
	return NewCreateSnapshotAction(opts).Run()
}

type startCVDHandler struct {
	Config Config
	OM     OperationManager
}

func newStartCVDHandler(c Config, om OperationManager) *startCVDHandler {
	return &startCVDHandler{Config: c, OM: om}
}

func (h *startCVDHandler) Handle(r *http.Request) (interface{}, error) {
	req := &apiv1.StartCVDRequest{}
	err := json.NewDecoder(r.Body).Decode(req)
	if err != nil {
		return nil, operator.NewBadRequestError("Malformed JSON in request", err)
	}
	vars := mux.Vars(r)
	group := vars["group"]
	name := vars["name"]
	opts := StartCVDActionOpts{
		Request:          req,
		Selector:         cvd.Selector{Group: group, Instance: name},
		Paths:            h.Config.Paths,
		OperationManager: h.OM,
		ExecContext:      exec.CommandContext,
	}
	return NewStartCVDAction(opts).Run()
}

type getCVDLogsHandler struct {
	Config Config
}

func (h *getCVDLogsHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	log.Println("request:", r.Method, r.URL.Path)
	vars := mux.Vars(r)
	group := vars["group"]
	name := vars["name"]
	logsDir, err := CVDLogsDir(exec.CommandContext, group, name)
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
	sp := fmt.Sprintf("/cvds/%s/%s/logs", group, name)
	handler := http.StripPrefix(sp, http.FileServer(http.Dir(logsDir)))
	handler.ServeHTTP(w, r)
}

type listOperationsHandler struct {
	om OperationManager
}

func (h *listOperationsHandler) Handle(r *http.Request) (interface{}, error) {
	operations := h.om.ListRunning()
	res := &apiv1.ListOperationsResponse{
		Operations: operations,
	}
	return res, nil
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

// GetOperationResult godoc
//
//	@Summary		Get operation result
//	@Description	The returned object varies depending on the type of the operation
//	@Produce		json
//	@Param			name	path		string		true	"Operation name"
//	@Success		200		{object}	interface{}	"The returned object varies depending on the type of the operation"
//	@Router			/operations/{name}/result [get]
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

type createCVDBugReportHandler struct {
	Config Config
	OM     OperationManager
}

func newCreateCVDBugReportHandler(c Config, om OperationManager) *createCVDBugReportHandler {
	return &createCVDBugReportHandler{
		Config: c,
		OM:     om,
	}
}

func (h *createCVDBugReportHandler) Handle(r *http.Request) (interface{}, error) {
	vars := mux.Vars(r)
	includeADBBugreport := false
	if _, ok := r.URL.Query()[URLQueryKeyIncludeAdbBugReport]; ok {
		includeADBBugreport = r.URL.Query().Get(URLQueryKeyIncludeAdbBugReport) != "false"
	}
	opts := CreateCVDBugReportActionOpts{
		Group:               vars["group"],
		IncludeADBBugreport: includeADBBugreport,
		Paths:               h.Config.Paths,
		OperationManager:    h.OM,
		ExecContext:         exec.CommandContext,
		UUIDGen:             func() string { return uuid.New().String() },
	}
	return NewCreateCVDBugReportAction(opts).Run()
}

type downloadCVDBugReportHandler struct {
	Config Config
}

func (h *downloadCVDBugReportHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	log.Printf("handling request(%p) started %s %s", r, r.Method, r.URL.Path)
	defer log.Printf("handling request(%p) ended", r)
	vars := mux.Vars(r)
	id := vars["uuid"]
	if err := uuid.Validate(id); err != nil {
		log.Printf("request %q failed with error: %v", r.Method+" "+r.URL.Path, err)
		replyJSONErr(w, err)
		return
	}
	filename := filepath.Join(h.Config.Paths.CVDBugReportsDir, id, BugReportZipFileName)
	http.ServeFile(w, r, filename)
}

type deleteCVDBugReportHandler struct {
	Config Config
}

func (h *deleteCVDBugReportHandler) Handle(r *http.Request) (interface{}, error) {
	vars := mux.Vars(r)
	id := vars["uuid"]
	if err := uuid.Validate(id); err != nil {
		return nil, err
	}
	dirName := filepath.Join(h.Config.Paths.CVDBugReportsDir, id)
	err := os.RemoveAll(dirName)
	return nil, err
}

type deleteSnapshotHandler struct {
	Config Config
	OM     OperationManager
}

func (h *deleteSnapshotHandler) Handle(r *http.Request) (interface{}, error) {
	vars := mux.Vars(r)
	id := vars["id"]
	if err := ValidateSnapshotID(id); err != nil {
		return nil, operator.NewBadRequestError("invalid request", err)
	}
	op := h.OM.New()
	go func() {
		dir := filepath.Join(h.Config.Paths.SnapshotsRootDir, id)
		result := &OperationResult{}
		result.Error = os.RemoveAll(dir)
		if err := h.OM.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}()
	return op, nil
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
	m    UserArtifactsManager
	isV1 bool
}

func parseFormValueInt(r *http.Request, key string) (int64, error) {
	valueRaw := r.FormValue(key)
	value, err := strconv.ParseInt(valueRaw, 10, 64)
	if err != nil {
		return -1, operator.NewBadRequestError(fmt.Sprintf("Invalid %s form field value: %q", key, valueRaw), err)
	}
	return value, nil
}

func (h *createUpdateUserArtifactHandler) Handle(r *http.Request) (interface{}, error) {
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
	vars := mux.Vars(r)
	f, fheader, err := r.FormFile("file")
	if err != nil {
		return nil, err
	}
	defer f.Close()
	chunkOffsetBytes, err := parseFormValueInt(r, "chunk_offset_bytes")
	if h.isV1 {
		if err != nil {
			return nil, err
		}
		fileSizeBytes, err := parseFormValueInt(r, "file_size_bytes")
		if err != nil {
			return nil, err
		}
		chunk := UserArtifactChunk{
			File:          f,
			Name:          fheader.Filename,
			OffsetBytes:   chunkOffsetBytes,
			SizeBytes:     fheader.Size,
			FileSizeBytes: fileSizeBytes,
		}
		return nil, h.m.UpdateArtifact(vars["checksum"], chunk)
	} else {
		dir := vars["name"]
		if err := ValidateFileName(dir); err != nil {
			return nil, operator.NewBadRequestError("invalid directory name", err)
		}
		if err != nil {
			log.Println("use of deprecated `chunk_number`")
			chunkNumberRaw := r.FormValue("chunk_number")
			chunkNumber, err := strconv.Atoi(chunkNumberRaw)
			if err != nil {
				return nil, operator.NewBadRequestError(
					fmt.Sprintf("Invalid chunk_number form field value: %q", chunkNumberRaw), err)
			}
			if chunkNumber < 0 {
				return nil, operator.NewBadRequestError(fmt.Sprintf("invalid value (chunk_number:%d)", chunkNumber), nil)
			}
			chunkSizeBytes, err := parseFormValueInt(r, "chunk_size_bytes")
			if err != nil {
				return nil, err
			}
			if chunkNumber < 0 {
				return nil, operator.NewBadRequestError(fmt.Sprintf("invalid value (chunk_size_bytes:%d)", chunkSizeBytes), nil)
			}
			chunkOffsetBytes = int64(chunkNumber-1) * chunkSizeBytes
		}
		chunk := UserArtifactChunk{
			Name:        fheader.Filename,
			File:        f,
			OffsetBytes: chunkOffsetBytes,
		}
		return nil, h.m.UpdateArtifactWithDir(dir, chunk)
	}
}

type statUserArtifactHandler struct {
	m UserArtifactsManager
}

func (h *statUserArtifactHandler) Handle(r *http.Request) (interface{}, error) {
	return h.m.StatArtifact(mux.Vars(r)["checksum"])
}

type extractUserArtifactHandler struct {
	om   OperationManager
	uam  UserArtifactsManager
	isV1 bool
}

func (h *extractUserArtifactHandler) Handle(r *http.Request) (interface{}, error) {
	vars := mux.Vars(r)
	if !h.isV1 {
		if err := ValidateFileName(vars["dir"]); err != nil {
			return nil, operator.NewBadRequestError("invalid directory name", err)
		}
		if err := ValidateFileName(vars["name"]); err != nil {
			return nil, operator.NewBadRequestError("invalid artifact name", err)
		}
	}
	op := h.om.New()
	go func() {
		var err error
		if h.isV1 {
			err = h.uam.ExtractArtifact(vars["checksum"])
		} else {
			err = h.uam.ExtractArtifactWithDir(vars["dir"], vars["name"])
		}
		if err := h.om.Complete(op.Name, &OperationResult{Error: err}); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}()
	return op, nil
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

func getFetchCredentials(r *http.Request) cvd.FetchCredentials {
	creds := cvd.FetchCredentials{}
	accessToken := r.Header.Get(HeaderBuildAPICreds)
	if accessToken != "" {
		creds.AccessTokenCredentials = cvd.AccessTokenCredentials{
			AccessToken:   accessToken,
			UserProjectID: r.Header.Get(HeaderUserProject),
		}
	} else {
		log.Printf("fetch credentials: no access token provided by client")
		if isRunningOnGCE() {
			log.Println("fetch credentials: running on gce")
			if ok, err := hasServiceAccountAccessToken(); err != nil {
				log.Printf("fetch credentials: service account token check failed: %s", err)
			} else if ok {
				log.Println("fetch credentials: using gce service account credentials")
				creds.UseGCEServiceAccountCredentials = true
			}
		}
	}
	return creds
}
