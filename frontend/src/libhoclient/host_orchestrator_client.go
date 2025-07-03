// Copyright 2023 Google LLC
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

package libhoclient

import (
	"crypto/sha256"
	"crypto/tls"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"time"

	wclient "github.com/google/android-cuttlefish/frontend/src/libhoclient/webrtcclient"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	opapi "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/gorilla/websocket"
	"github.com/pion/webrtc/v3"
)

type ApiCallError struct {
	opapi.ErrorMsg

	HTTPStatusCode int
}

func (e *ApiCallError) Error() string {
	str := fmt.Sprintf("api call error: message: %s", e.ErrorMsg.Error)
	if e.Details != "" {
		str += fmt.Sprintf("\n\nDETAILS: %s", e.Details)
	}
	return str
}

type StatusCodeError struct {
	Code int
}

func (e *StatusCodeError) Error() string {
	if e.Code >= 400 && e.Code <= 499 {
		return fmt.Sprintf("client error: status code: %d", e.Code)
	}
	if e.Code >= 500 && e.Code <= 599 {
		return fmt.Sprintf("server error: status code: %d", e.Code)
	}
	return fmt.Sprintf("status code: %d", e.Code)
}

type ConnectWebRTCOpts struct {
	LocalICEConfig *wclient.ICEConfig
}

type BuildAPICreds interface {
	ApplyToHTTPRequest(rb *HTTPRequestBuilder)
}

type AccessTokenBuildAPICreds struct {
	AccessToken   string
	UserProjectID string
}

func (c *AccessTokenBuildAPICreds) ApplyToHTTPRequest(rb *HTTPRequestBuilder) {
	const HTTPHeaderBuildAPICreds = "X-Cutf-Host-Orchestrator-BuildAPI-Creds"
	const HTTPHeaderBuildAPICredsUserProjectID = "X-Cutf-Host-Orchestrator-BuildAPI-Creds-User-Project-ID"
	if c.AccessToken != "" {
		rb.AddHeader(HTTPHeaderBuildAPICreds, c.AccessToken)
		if c.UserProjectID != "" {
			rb.AddHeader(HTTPHeaderBuildAPICredsUserProjectID, c.UserProjectID)
		}
	}
}

type CreateBugReportOpts struct {
	IncludeADBBugReport bool
}

// Manage cuttlefish instances.
type InstancesClient interface {
	// Lists currently running devices.
	ListCVDs() ([]*hoapi.CVD, error)
	// Calls cvd fetch in the remote host, the downloaded artifacts can be used to create a CVD later.
	// If not empty, the provided credentials will be used by the host orchestrator to access the build api.
	FetchArtifacts(req *hoapi.FetchArtifactsRequest, creds BuildAPICreds) (*hoapi.FetchArtifactsResponse, error)
	// Create a new device with artifacts from the build server or previously uploaded by the user.
	// If not empty, the provided credentials will be used to download necessary artifacts from the build api.
	CreateCVD(req *hoapi.CreateCVDRequest, creds BuildAPICreds) (*hoapi.CreateCVDResponse, error)
	CreateCVDOp(req *hoapi.CreateCVDRequest, creds BuildAPICreds) (*hoapi.Operation, error)
	// Deletes an existing cvd instance.
	DeleteCVD(id string) error
}

// Manage artifacts created by the user.
type UserArtifactsClient interface {
	// Upload artifact into the artifacts repository.
	// Artifacts are identified by their SHA256 checksum in the artifacts repository
	UploadArtifact(filename string) error
	// Extract artifact into the artifacts repository.
	// Artifacts are identified by their SHA256 checksum in the artifacts repository
	ExtractArtifact(filename string) (*hoapi.Operation, error)
	// Creates a directory in the host where user artifacts can be uploaded to.
	CreateUploadDir() (string, error)
	// Uploads file into the given directory.
	UploadFile(uploadDir string, filename string) error
	UploadFileWithOptions(uploadDir string, filename string, options UploadOptions) error
	// Extracts a compressed file.
	ExtractFile(uploadDir string, filename string) (*hoapi.Operation, error)
}

// Operations that could be performend on a given instance.
type InstanceOperationsClient interface {
	// Create cvd bugreport.
	CreateBugReport(group string, opts CreateBugReportOpts, dst io.Writer) error
	// Powerwash the device.
	Powerwash(groupName, instanceName string) error
	// Stop the device.
	Stop(groupName, instanceName string) error
	// Press power button.
	Powerbtn(groupName, instanceName string) error
	// Start the device.
	Start(groupName, instanceName string, req *hoapi.StartCVDRequest) error
	// Create device snapshot.
	CreateSnapshot(groupName, instanceName string, req *hoapi.CreateSnapshotRequest) (*hoapi.CreateSnapshotResponse, error)
}

// Manage direct two-way communication channels with remote instances.
type InstanceConnectionsClient interface {
	// Creates a webRTC connection to a device running in this host.
	ConnectWebRTC(device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error)
	// Connect to ADB WebSocket endpoint.
	ConnectADBWebSocket(device string) (*websocket.Conn, error)
}

// Manage the `operation` resource in the HO API used to track lengthy actions.
type OperationsClient interface {
	// Wait for an operation, `result` will be populated with the relevant operation's result object.
	WaitForOperation(name string, result any) error
}

// Manages snapshots.
type SnapshotsClient interface {
	// Create device snapshot.
	DeleteSnapshot(id string) error
}

// A client to the host orchestrator service running in a remote host.
type HostOrchestratorClient interface {
	InstancesClient
	UserArtifactsClient
	InstanceOperationsClient
	InstanceConnectionsClient
	OperationsClient
	SnapshotsClient
}

const (
	HTTPHeaderBuildAPICreds              = "X-Cutf-Host-Orchestrator-BuildAPI-Creds"
	HTTPHeaderBuildAPICredsUserProjectID = "X-Cutf-Host-Orchestrator-BuildAPI-Creds-User-Project-ID"
	// If used, the Cloud Orchestrator proxy would set/override the "X-Cutf-Host-Orchestrator-BuildAPI-Creds"
	// http header.
	HTTPHeaderCOInjectBuildAPICreds = "X-Cutf-Cloud-Orchestrator-Inject-BuildAPI-Creds"
)

func NewHostOrchestratorClient(url string) *HostOrchestratorClientImpl {
	return &HostOrchestratorClientImpl{
		HTTPHelper: HTTPHelper{
			Client:       http.DefaultClient,
			RootEndpoint: url,
		},
	}
}

type HostOrchestratorClientImpl struct {
	HTTPHelper HTTPHelper
	ProxyURL   string
}

func (c *HostOrchestratorClientImpl) getInfraConfig() (*opapi.InfraConfig, error) {
	var res opapi.InfraConfig
	if err := c.HTTPHelper.NewGetRequest("/infra_config").JSONResDo(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *HostOrchestratorClientImpl) ConnectWebRTC(device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error) {
	polledConn, err := c.createPolledConnection(device)
	if err != nil {
		return nil, fmt.Errorf("failed to create polled connection: %w", err)
	}
	iceServers := []webrtc.ICEServer{}
	if opts.LocalICEConfig != nil {
		iceServers = append(iceServers, opts.LocalICEConfig.ICEServers...)
	}
	infraConfig, err := c.getInfraConfig()
	if err != nil {
		return nil, fmt.Errorf("failed to obtain infra config: %w", err)
	}
	iceServers = append(iceServers, asWebRTCICEServers(infraConfig.IceServers)...)
	signaling := c.initHandling(polledConn.ConnId, iceServers, logger)
	conn, err := wclient.NewConnectionWithLogger(&signaling, observer, logger)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to device over webrtc: %w", err)
	}
	return conn, nil
}

func (c *HostOrchestratorClientImpl) ConnectADBWebSocket(device string) (*websocket.Conn, error) {
	// Connect to the ADB proxy WebSocket endpoint for the device under the host.
	//   wss://127.0.0.1:8080/v1/zones/local/hosts/.../devices/cvd-1-1/adb
	dialer := websocket.Dialer{
		TLSClientConfig: &tls.Config{
			InsecureSkipVerify: true,
		},
	}
	if c.ProxyURL != "" {
		proxyURL, err := url.Parse(c.ProxyURL)
		if err != nil {
			return nil, fmt.Errorf("failed to parse proxy %s: %w", c.ProxyURL, err)
		}
		dialer.Proxy = http.ProxyURL(proxyURL)
	}

	url, err := url.Parse(c.HTTPHelper.RootEndpoint)
	if err != nil {
		return nil, fmt.Errorf("failed to parse URL %s: %w", c.HTTPHelper.RootEndpoint, err)
	}
	switch p := &url.Scheme; *p {
	case "https":
		*p = "wss"
	case "http":
		*p = "ws"
	default:
		return nil, fmt.Errorf("unknown scheme %s", *p)
	}
	url = url.JoinPath("devices", device, "adb")

	// Get auth header for WebSocket connection
	rb := c.HTTPHelper.NewGetRequest("")
	if err := rb.setAuthz(); err != nil {
		return nil, fmt.Errorf("failed to set authorization header: %w", err)
	}
	// Connect to the WebSocket
	wsConn, _, err := dialer.Dial(url.String(), rb.request.Header)
	if err != nil {
		return nil, fmt.Errorf("failed to connect WebSocket %s: %w", url.String(), err)
	}
	return wsConn, nil
}

func (c *HostOrchestratorClientImpl) initHandling(connID string, iceServers []webrtc.ICEServer, logger io.Writer) wclient.Signaling {
	sendCh := make(chan any)
	recvCh := make(chan map[string]any)

	// The forwarding goroutine will close this channel and stop when the send
	// channel is closed, which will cause the polling go routine to close its own
	// channel and stop as well.
	stopPollCh := make(chan bool)
	go c.webRTCPoll(recvCh, connID, stopPollCh, logger)
	go c.webRTCForward(sendCh, connID, stopPollCh, logger)

	return wclient.Signaling{
		SendCh:           sendCh,
		RecvCh:           recvCh,
		ICEServers:       iceServers,
		ClientICEServers: iceServers,
	}
}

const (
	initialPollInterval  = 100 * time.Millisecond
	maxPollInterval      = 2 * time.Second
	maxConsecutiveErrors = 10
)

func (c *HostOrchestratorClientImpl) webRTCPoll(sinkCh chan map[string]any, connID string, stopCh chan bool, logger io.Writer) {
	start := 0
	pollInterval := initialPollInterval
	errCount := 0
	for {
		path := fmt.Sprintf("/polled_connections/%s/messages?start=%d", connID, start)
		var messages []map[string]any
		if err := c.HTTPHelper.NewGetRequest(path).JSONResDo(&messages); err != nil {
			fmt.Fprintf(logger, "Error polling messages: %v\n", err)
			errCount++
			if errCount >= maxConsecutiveErrors {
				fmt.Fprintln(logger, "Reached maximum number of consecutive polling errors, exiting")
				close(sinkCh)
				return
			}
		} else {
			errCount = 0
		}
		if len(messages) > 0 {
			pollInterval = initialPollInterval
		} else {
			pollInterval = 2 * pollInterval
			if pollInterval > maxPollInterval {
				pollInterval = maxPollInterval
			}
		}
		for _, message := range messages {
			if message["message_type"] != "device_msg" {
				fmt.Fprintf(logger, "unexpected message type: %s\n", message["message_type"])
				continue
			}
			sinkCh <- message["payload"].(map[string]any)
			start++
		}
		select {
		case <-stopCh:
			// The forwarding goroutine has requested a stop
			close(sinkCh)
			return
		case <-time.After(pollInterval):
			// poll for messages again
		}
	}
}

func (c *HostOrchestratorClientImpl) webRTCForward(srcCh chan any, connID string, stopPollCh chan bool, logger io.Writer) {
	for {
		msg, open := <-srcCh
		if !open {
			// The webrtc client closed the channel
			close(stopPollCh)
			break
		}
		forwardMsg := opapi.ForwardMsg{Payload: msg}
		path := fmt.Sprintf("/polled_connections/%s/:forward", connID)
		i := 0
		for ; i < maxConsecutiveErrors; i++ {
			rb := c.HTTPHelper.NewPostRequest(path, &forwardMsg)
			if err := rb.JSONResDo(nil); err != nil {
				fmt.Fprintf(logger, "Error sending message to device: %v\n", err)
			} else {
				break
			}
		}
		if i == maxConsecutiveErrors {
			fmt.Fprintln(logger, "Reached maximum number of sending errors, exiting")
			close(stopPollCh)
			return
		}
	}
}

func (c *HostOrchestratorClientImpl) createPolledConnection(device string) (*opapi.NewConnReply, error) {
	var res opapi.NewConnReply
	rb := c.HTTPHelper.NewPostRequest("/polled_connections", &opapi.NewConnMsg{DeviceId: device})
	if err := rb.JSONResDo(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *HostOrchestratorClientImpl) WaitForOperation(name string, res any) error {
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusServiceUnavailable, http.StatusGatewayTimeout},
		RetryDelay:  5 * time.Second,
		MaxWait:     2 * time.Minute,
	}
	return c.waitForOperation(name, res, retryOpts)
}

func (c *HostOrchestratorClientImpl) waitForOperation(name string, res any, retryOpts RetryOptions) error {
	path := "/operations/" + name + "/:wait"
	return c.HTTPHelper.NewPostRequest(path, nil).JSONResDoWithRetries(res, retryOpts)
}

func (c *HostOrchestratorClientImpl) FetchArtifacts(req *hoapi.FetchArtifactsRequest, creds BuildAPICreds) (*hoapi.FetchArtifactsResponse, error) {
	var op hoapi.Operation
	rb := c.HTTPHelper.NewPostRequest("/artifacts", req)
	creds.ApplyToHTTPRequest(rb)
	if err := rb.JSONResDo(&op); err != nil {
		return nil, err
	}

	res := &hoapi.FetchArtifactsResponse{}
	if err := c.WaitForOperation(op.Name, &res); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *HostOrchestratorClientImpl) CreateCVDOp(req *hoapi.CreateCVDRequest, creds BuildAPICreds) (*hoapi.Operation, error) {
	op := &hoapi.Operation{}
	rb := c.HTTPHelper.NewPostRequest("/cvds", req)
	creds.ApplyToHTTPRequest(rb)
	if err := rb.JSONResDo(op); err != nil {
		return nil, err
	}
	return op, nil
}

func (c *HostOrchestratorClientImpl) CreateCVD(req *hoapi.CreateCVDRequest, creds BuildAPICreds) (*hoapi.CreateCVDResponse, error) {
	op, err := c.CreateCVDOp(req, creds)
	if err != nil {
		return nil, err
	}
	res := &hoapi.CreateCVDResponse{}
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusServiceUnavailable, http.StatusGatewayTimeout},
		RetryDelay:  30 * time.Second,
		MaxWait:     10 * time.Minute,
	}
	if err := c.waitForOperation(op.Name, &res, retryOpts); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *HostOrchestratorClientImpl) DeleteCVD(id string) error {
	var op hoapi.Operation
	rb := c.HTTPHelper.NewDeleteRequest("/cvds/" + id)
	if err := rb.JSONResDo(&op); err != nil {
		return err
	}
	res := &hoapi.StopCVDResponse{}
	if err := c.WaitForOperation(op.Name, &res); err != nil {
		return err
	}
	return nil
}

func (c *HostOrchestratorClientImpl) ListCVDs() ([]*hoapi.CVD, error) {
	var res hoapi.ListCVDsResponse
	if err := c.HTTPHelper.NewGetRequest("/cvds").JSONResDo(&res); err != nil {
		return nil, err
	}
	return res.CVDs, nil
}

func (c *HostOrchestratorClientImpl) Powerwash(groupName, instanceName string) error {
	path := fmt.Sprintf("/cvds/%s/%s/:powerwash", groupName, instanceName)
	rb := c.HTTPHelper.NewPostRequest(path, nil)
	return c.doEmptyResponseRequest(rb)
}

func (c *HostOrchestratorClientImpl) Powerbtn(groupName, instanceName string) error {
	path := fmt.Sprintf("/cvds/%s/%s/:powerbtn", groupName, instanceName)
	rb := c.HTTPHelper.NewPostRequest(path, nil)
	return c.doEmptyResponseRequest(rb)
}

func (c *HostOrchestratorClientImpl) Stop(groupName, instanceName string) error {
	path := fmt.Sprintf("/cvds/%s/%s/:stop", groupName, instanceName)
	rb := c.HTTPHelper.NewPostRequest(path, nil)
	return c.doEmptyResponseRequest(rb)
}

func (c *HostOrchestratorClientImpl) Start(groupName, instanceName string, req *hoapi.StartCVDRequest) error {
	path := fmt.Sprintf("/cvds/%s/%s/:start", groupName, instanceName)
	rb := c.HTTPHelper.NewPostRequest(path, req)
	return c.doEmptyResponseRequest(rb)
}

func (c *HostOrchestratorClientImpl) CreateSnapshot(groupName, instanceName string, req *hoapi.CreateSnapshotRequest) (*hoapi.CreateSnapshotResponse, error) {
	path := fmt.Sprintf("/cvds/%s/%s/snapshots", groupName, instanceName)
	rb := c.HTTPHelper.NewPostRequest(path, req)
	op := &hoapi.Operation{}
	if err := rb.JSONResDo(op); err != nil {
		return nil, err
	}
	res := &hoapi.CreateSnapshotResponse{}
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusServiceUnavailable, http.StatusGatewayTimeout},
		RetryDelay:  30 * time.Second,
		MaxWait:     10 * time.Minute,
	}
	if err := c.waitForOperation(op.Name, &res, retryOpts); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *HostOrchestratorClientImpl) DeleteSnapshot(id string) error {
	path := fmt.Sprintf("/snapshots/%s", id)
	rb := c.HTTPHelper.NewDeleteRequest(path)
	op := &hoapi.Operation{}
	if err := rb.JSONResDo(op); err != nil {
		return err
	}
	return c.WaitForOperation(op.Name, nil)
}

func (c *HostOrchestratorClientImpl) doEmptyResponseRequest(rb *HTTPRequestBuilder) error {
	op := &hoapi.Operation{}
	if err := rb.JSONResDo(op); err != nil {
		return err
	}
	res := &hoapi.EmptyResponse{}
	if err := c.WaitForOperation(op.Name, &res); err != nil {
		return err
	}
	return nil
}

func (c *HostOrchestratorClientImpl) CreateUploadDir() (string, error) {
	uploadDir := &hoapi.UploadDirectory{}
	if err := c.HTTPHelper.NewPostRequest("/userartifacts", nil).JSONResDo(uploadDir); err != nil {
		return "", err
	}
	return uploadDir.Name, nil
}

func (c *HostOrchestratorClientImpl) UploadFile(uploadDir string, filename string) error {
	return c.UploadFileWithOptions(uploadDir, filename, DefaultUploadOptions())
}

func (c *HostOrchestratorClientImpl) UploadFileWithOptions(uploadDir string, filename string, uploadOpts UploadOptions) error {
	return c.upload("/userartifacts/"+uploadDir, filename, uploadOpts)
}

func sha256Checksum(filename string) (string, error) {
	f, err := os.Open(filename)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return fmt.Sprintf("%x", h.Sum(nil)), nil
}

func (c *HostOrchestratorClientImpl) UploadArtifact(filename string) error {
	checksum, err := sha256Checksum(filename)
	if err != nil {
		return err
	}
	path := "/v1/userartifacts/" + checksum
	res := &hoapi.StatArtifactResponse{}
	// Early return if the artifact already exist.
	if err := c.HTTPHelper.NewGetRequest(path).JSONResDo(res); err == nil {
		return nil
	}
	return c.upload("/v1/userartifacts/"+checksum, filename, DefaultUploadOptions())
}

func DefaultUploadOptions() UploadOptions {
	return UploadOptions{
		BackOffOpts: ExpBackOffOptions{
			InitialDuration:     500 * time.Millisecond,
			RandomizationFactor: 0.5,
			Multiplier:          1.5,
			MaxElapsedTime:      2 * time.Minute,
		},
		ChunkSizeBytes: 16 * 1024 * 1024, // 16 MB
		NumWorkers:     32,
	}
}

func (c *HostOrchestratorClientImpl) upload(endpoint, filename string, uploadOpts UploadOptions) error {
	if uploadOpts.ChunkSizeBytes == 0 {
		panic("ChunkSizeBytes value cannot be zero")
	}
	if uploadOpts.NumWorkers == 0 {
		panic("NumWorkers value cannot be zero")
	}
	if uploadOpts.BackOffOpts.MaxElapsedTime == 0 {
		panic("MaxElapsedTime value cannot be zero")
	}
	uploader := &FilesUploader{
		HTTPHelper:     c.HTTPHelper,
		DumpOut:        c.HTTPHelper.Dumpster,
		UploadEndpoint: endpoint,
		UploadOptions:  uploadOpts,
	}
	return uploader.Upload([]string{filename})
}

func (c *HostOrchestratorClientImpl) ExtractFile(uploadDir string, filename string) (*hoapi.Operation, error) {
	result := &hoapi.Operation{}
	rb := c.HTTPHelper.NewPostRequest("/userartifacts/"+uploadDir+"/"+filename+"/:extract", nil)
	if err := rb.JSONResDo(result); err != nil {
		return nil, err
	}
	return result, nil
}

func (c *HostOrchestratorClientImpl) ExtractArtifact(filename string) (*hoapi.Operation, error) {
	checksum, err := sha256Checksum(filename)
	if err != nil {
		return nil, err
	}
	res := &hoapi.StatArtifactResponse{}
	if err := c.HTTPHelper.NewGetRequest("/v1/userartifacts/" + checksum).JSONResDo(res); err != nil {
		return nil, err
	}
	op := &hoapi.Operation{}
	if err := c.HTTPHelper.NewPostRequest("/v1/userartifacts/"+checksum+"/:extract", nil).JSONResDo(op); err != nil {
		return nil, err
	}
	return op, nil
}

func (c *HostOrchestratorClientImpl) CreateBugReport(group string, opts CreateBugReportOpts, dst io.Writer) error {
	op := &hoapi.Operation{}
	path := "/cvds/" + group + "/:bugreport"
	if opts.IncludeADBBugReport {
		path = path + "?include_adb_bugreport=true"
	}
	rb := c.HTTPHelper.NewPostRequest(path, nil)
	if err := rb.JSONResDo(op); err != nil {
		return err
	}
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusServiceUnavailable, http.StatusGatewayTimeout},
		RetryDelay:  30 * time.Second,
		MaxWait:     3 * time.Minute,
	}
	uuid := ""
	if err := c.waitForOperation(op.Name, &uuid, retryOpts); err != nil {
		return err
	}
	req, err := http.NewRequest("GET", c.HTTPHelper.RootEndpoint+"/cvdbugreports/"+uuid, nil)
	if err != nil {
		return err
	}
	res, err := c.HTTPHelper.Client.Do(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	if _, err := io.Copy(dst, res.Body); err != nil {
		return err
	}
	if res.StatusCode < 200 || res.StatusCode > 299 {
		return &ApiCallError{ErrorMsg: opapi.ErrorMsg{Error: res.Status}}
	}
	return nil
}

// Gather services logs.
type ServiceLogsClient interface {
	PullHOLogs(dst io.Writer) error
}

// Implementation of `ServiceLogsClient` collecting logs via systemd-journal-gatewayd HTTP API.
// https://www.freedesktop.org/software/systemd/man/latest/systemd-journal-gatewayd.service.html#Supported%20URLs
type JournalGatewaydClient struct {
	HTTPHelper HTTPHelper
	ProxyURL   string
}

func NewJournalGatewaydClient(url string) *JournalGatewaydClient {
	return &JournalGatewaydClient{
		HTTPHelper: HTTPHelper{
			Client:       http.DefaultClient,
			RootEndpoint: url,
		},
	}
}

func (c *JournalGatewaydClient) PullHOLogs(dst io.Writer) error {
	const srvName = "cuttlefish-host_orchestrator.service"
	path := fmt.Sprintf("/_journal/entries?_SYSTEMD_UNIT=%s", srvName)
	req, err := http.NewRequest("GET", c.HTTPHelper.RootEndpoint+path, nil)
	if err != nil {
		return err
	}
	res, err := c.HTTPHelper.Client.Do(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	if _, err := io.Copy(dst, res.Body); err != nil {
		return err
	}
	if res.StatusCode < 200 || res.StatusCode > 299 {
		return &StatusCodeError{Code: res.StatusCode}
	}
	return nil
}

func asWebRTCICEServers(in []opapi.IceServer) []webrtc.ICEServer {
	out := []webrtc.ICEServer{}
	for _, s := range in {
		out = append(out, webrtc.ICEServer{
			URLs:       s.URLs,
			Username:   s.Username,
			Credential: s.Credential,
		})
	}
	return out
}
