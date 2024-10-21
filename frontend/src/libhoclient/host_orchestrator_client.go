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
package client

import (
	"crypto/tls"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"time"

	wclient "github.com/google/cloud-android-orchestration/pkg/webrtcclient"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	opapi "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/gorilla/websocket"
	"github.com/pion/webrtc/v3"
)

type ApiCallError struct {
	Code     int    `json:"code,omitempty"`
	ErrorMsg string `json:"error,omitempty"`
	Details  string `json:"details,omitempty"`
}

func (e *ApiCallError) Error() string {
	str := fmt.Sprintf("api call error %d: %s", e.Code, e.ErrorMsg)
	if e.Details != "" {
		str += fmt.Sprintf("\n\nDETAILS: %s", e.Details)
	}
	return str
}

type ConnectWebRTCOpts struct {
	LocalICEConfig *wclient.ICEConfig
}

type BuildAPICredential struct {
	AccessToken   string
	UserProjectID string
}

// A client to the host orchestrator service running in a remote host.
type HostOrchestratorService interface {
	// Lists currently running devices.
	ListCVDs() ([]*hoapi.CVD, error)

	// Creates a directory in the host where user artifacts can be uploaded to.
	CreateUploadDir() (string, error)

	// Uploads file into the given directory.
	UploadFile(uploadDir string, filename string) error
	UploadFileWithOptions(uploadDir string, filename string, options UploadOptions) error

	// Extracts a compressed file.
	ExtractFile(uploadDir string, filename string) (*hoapi.Operation, error)

	// Create a new device with artifacts from the build server or previously uploaded by the user.
	// If not empty, the provided credentials will be used to download necessary artifacts from the build api.
	CreateCVD(req *hoapi.CreateCVDRequest, buildAPICredentials BuildAPICredential) (*hoapi.CreateCVDResponse, error)
	CreateCVDOp(req *hoapi.CreateCVDRequest, buildAPICredentials BuildAPICredential) (*hoapi.Operation, error)

	// Deletes an existing cvd instance.
	DeleteCVD(id string) error

	// Calls cvd fetch in the remote host, the downloaded artifacts can be used to create a CVD later.
	// If not empty, the provided credentials will be used by the host orchestrator to access the build api.
	FetchArtifacts(req *hoapi.FetchArtifactsRequest, buildAPICredentials BuildAPICredential) (*hoapi.FetchArtifactsResponse, error)

	// Creates a webRTC connection to a device running in this host.
	ConnectWebRTC(device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error)

	// Connect to ADB WebSocket endpoint.
	ConnectADBWebSocket(device string) (*websocket.Conn, error)

	// Wait for an operation, `result` will be populated with the relevant operation's result object.
	WaitForOperation(name string, result any) error

	// Create cvd bugreport.
	CreateBugreport(group string, dst io.Writer) error
}

const DefaultHostOrchestratorCredentialsHeader = "X-Cutf-Host-Orchestrator-BuildAPI-Creds"
const BuildAPICredsUserProjectIDHeader = "X-Cutf-Host-Orchestrator-BuildAPI-Creds-User-Project-ID"

func NewHostOrchestratorService(url string) HostOrchestratorService {
	return &HostOrchestratorServiceImpl{
		HTTPHelper: HTTPHelper{
			Client:       http.DefaultClient,
			RootEndpoint: url,
		},
		BuildAPICredentialsHeader: DefaultHostOrchestratorCredentialsHeader,
	}
}

type HostOrchestratorServiceImpl struct {
	HTTPHelper                HTTPHelper
	BuildAPICredentialsHeader string
	ProxyURL                  string
}

func (c *HostOrchestratorServiceImpl) getInfraConfig() (*opapi.InfraConfig, error) {
	var res opapi.InfraConfig
	if err := c.HTTPHelper.NewGetRequest("/infra_config").JSONResDo(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *HostOrchestratorServiceImpl) ConnectWebRTC(device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error) {
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

func (c *HostOrchestratorServiceImpl) ConnectADBWebSocket(device string) (*websocket.Conn, error) {
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
			return nil, fmt.Errorf("Failed to parse proxy %s: %w", c.ProxyURL, err)
		}
		dialer.Proxy = http.ProxyURL(proxyURL)
	}

	url, err := url.Parse(c.HTTPHelper.RootEndpoint)
	if err != nil {
		return nil, fmt.Errorf("Failed to parse URL %s: %w", c.HTTPHelper.RootEndpoint, err)
	}
	switch p := &url.Scheme; *p {
	case "https":
		*p = "wss"
	case "http":
		*p = "ws"
	default:
		return nil, fmt.Errorf("Unknown scheme %s", *p)
	}
	url = url.JoinPath("devices", device, "adb")

	// Get auth header for WebSocket connection
	rb := c.HTTPHelper.NewGetRequest("")
	if err := rb.setAuthz(); err != nil {
		return nil, fmt.Errorf("Failed to set authorization header: %w", err)
	}
	// Connect to the WebSocket
	wsConn, _, err := dialer.Dial(url.String(), rb.request.Header)
	if err != nil {
		return nil, fmt.Errorf("Failed to connect WebSocket %s: %w", url.String(), err)
	}
	return wsConn, nil
}

func (c *HostOrchestratorServiceImpl) initHandling(connID string, iceServers []webrtc.ICEServer, logger io.Writer) wclient.Signaling {
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

func (c *HostOrchestratorServiceImpl) webRTCPoll(sinkCh chan map[string]any, connID string, stopCh chan bool, logger io.Writer) {
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

func (c *HostOrchestratorServiceImpl) webRTCForward(srcCh chan any, connID string, stopPollCh chan bool, logger io.Writer) {
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

func (c *HostOrchestratorServiceImpl) createPolledConnection(device string) (*opapi.NewConnReply, error) {
	var res opapi.NewConnReply
	rb := c.HTTPHelper.NewPostRequest("/polled_connections", &opapi.NewConnMsg{DeviceId: device})
	if err := rb.JSONResDo(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *HostOrchestratorServiceImpl) WaitForOperation(name string, res any) error {
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusServiceUnavailable, http.StatusGatewayTimeout},
		RetryDelay:  5 * time.Second,
		MaxWait:     2 * time.Minute,
	}
	return c.waitForOperation(name, res, retryOpts)
}

func (c *HostOrchestratorServiceImpl) waitForOperation(name string, res any, retryOpts RetryOptions) error {
	path := "/operations/" + name + "/:wait"
	return c.HTTPHelper.NewPostRequest(path, nil).JSONResDoWithRetries(res, retryOpts)
}

func (c *HostOrchestratorServiceImpl) FetchArtifacts(req *hoapi.FetchArtifactsRequest, creds BuildAPICredential) (*hoapi.FetchArtifactsResponse, error) {
	var op hoapi.Operation
	rb := c.HTTPHelper.NewPostRequest("/artifacts", req)
	addBuildAPICredsHeaders(rb, creds)
	if err := rb.JSONResDo(&op); err != nil {
		return nil, err
	}

	res := &hoapi.FetchArtifactsResponse{}
	if err := c.WaitForOperation(op.Name, &res); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *HostOrchestratorServiceImpl) CreateCVDOp(req *hoapi.CreateCVDRequest, creds BuildAPICredential) (*hoapi.Operation, error) {
	op := &hoapi.Operation{}
	rb := c.HTTPHelper.NewPostRequest("/cvds", req)
	addBuildAPICredsHeaders(rb, creds)
	if err := rb.JSONResDo(op); err != nil {
		return nil, err
	}
	return op, nil
}

func (c *HostOrchestratorServiceImpl) CreateCVD(req *hoapi.CreateCVDRequest, creds BuildAPICredential) (*hoapi.CreateCVDResponse, error) {
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

func (c *HostOrchestratorServiceImpl) DeleteCVD(id string) error {
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

func (c *HostOrchestratorServiceImpl) ListCVDs() ([]*hoapi.CVD, error) {
	var res hoapi.ListCVDsResponse
	if err := c.HTTPHelper.NewGetRequest("/cvds").JSONResDo(&res); err != nil {
		return nil, err
	}
	return res.CVDs, nil
}

func (c *HostOrchestratorServiceImpl) CreateUploadDir() (string, error) {
	uploadDir := &hoapi.UploadDirectory{}
	if err := c.HTTPHelper.NewPostRequest("/userartifacts", nil).JSONResDo(uploadDir); err != nil {
		return "", err
	}
	return uploadDir.Name, nil
}

func (c *HostOrchestratorServiceImpl) UploadFile(uploadDir string, filename string) error {
	return c.UploadFileWithOptions(uploadDir, filename, DefaultUploadOptions())
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

func (c *HostOrchestratorServiceImpl) UploadFileWithOptions(uploadDir string, filename string, uploadOpts UploadOptions) error {
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
		HTTPHelper:    c.HTTPHelper,
		DumpOut:       c.HTTPHelper.Dumpster,
		UploadDir:     uploadDir,
		UploadOptions: uploadOpts,
	}
	return uploader.Upload([]string{filename})
}

func (c *HostOrchestratorServiceImpl) ExtractFile(uploadDir string, filename string) (*hoapi.Operation, error) {
	result := &hoapi.Operation{}
	rb := c.HTTPHelper.NewPostRequest("/userartifacts/"+uploadDir+"/"+filename+"/:extract", nil)
	if err := rb.JSONResDo(result); err != nil {
		return nil, err
	}
	return result, nil
}

func (c *HostOrchestratorServiceImpl) CreateBugreport(group string, dst io.Writer) error {
	op := &hoapi.Operation{}
	rb := c.HTTPHelper.NewPostRequest("/cvds/"+group+"/:bugreport", nil)
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
		return &ApiCallError{ErrorMsg: res.Status}
	}
	return nil
}

func asWebRTCICEServers(in []opapi.IceServer) []webrtc.ICEServer {
	out := []webrtc.ICEServer{}
	for _, s := range in {
		out = append(out, webrtc.ICEServer{
			URLs: s.URLs,
		})
	}
	return out
}

func addBuildAPICredsHeaders(rb *HTTPRequestBuilder, creds BuildAPICredential) {
	if creds.AccessToken != "" {
		rb.AddHeader(DefaultHostOrchestratorCredentialsHeader, creds.AccessToken)
		if creds.UserProjectID != "" {
			rb.AddHeader(BuildAPICredsUserProjectIDHeader, creds.UserProjectID)
		}
	}
}
