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
	"fmt"
	"io"
	"net/http"
	"time"

	wclient "github.com/google/cloud-android-orchestration/pkg/webrtcclient"

	hoapi "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/pion/webrtc/v3"
)

type ConnectWebRTCOpts struct {
	LocalICEConfig *wclient.ICEConfig
}

// A client to the host orchestrator service running in a remote host.
type HostOrchestratorService interface {
	// Lists currently running devices.
	ListCVDs() ([]*hoapi.CVD, error)

	// Creates a directory in the host where user artifacts can be uploaded to.
	CreateUploadDir() (string, error)

	// Uploads user files to a previously created directory.
	UploadFiles(uploadDir string, filenames []string) error

	UploadFilesWithOptions(uploadDir string, filenames []string, options UploadOptions) error

	// Create a new device with artifacts from the build server or previously uploaded by the user.
	// If not empty, the provided credentials will be used to download necessary artifacts from the build api.
	CreateCVD(req *hoapi.CreateCVDRequest, buildAPICredentials string) (*hoapi.CreateCVDResponse, error)

	// Calls cvd fetch in the remote host, the downloaded artifacts can be used to create a CVD later.
	// If not empty, the provided credentials will be used by the host orchestrator to access the build api.
	FetchArtifacts(req *hoapi.FetchArtifactsRequest, buildAPICredentials string) (*hoapi.FetchArtifactsResponse, error)

	// Downloads runtime artifacts tar file into `dst`.
	DownloadRuntimeArtifacts(dst io.Writer) error

	// Creates a webRTC connection to a device running in this host.
	ConnectWebRTC(device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error)
}

const defaultHostOrchestratorCredentialsHeader = "X-Cutf-Host-Orchestrator-BuildAPI-Creds"

func NewHostOrchestratoService(url string) HostOrchestratorService {
	return &HostOrchestratorServiceImpl{
		HTTPHelper: HTTPHelper{
			Client:       http.DefaultClient,
			RootEndpoint: url,
		},
		WaitRetries:               3,
		WaitRetryDelay:            5 * time.Second,
		BuildAPICredentialsHeader: defaultHostOrchestratorCredentialsHeader,
	}
}

type HostOrchestratorServiceImpl struct {
	HTTPHelper                HTTPHelper
	WaitRetries               uint
	WaitRetryDelay            time.Duration
	BuildAPICredentialsHeader string
}

func (c *HostOrchestratorServiceImpl) getInfraConfig() (*hoapi.InfraConfig, error) {
	var res hoapi.InfraConfig
	if err := c.HTTPHelper.NewGetRequest("/infra_config").Do(&res); err != nil {
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
		if err := c.HTTPHelper.NewGetRequest(path).Do(&messages); err != nil {
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
		forwardMsg := hoapi.ForwardMsg{Payload: msg}
		path := fmt.Sprintf("/polled_connections/%s/:forward", connID)
		i := 0
		for ; i < maxConsecutiveErrors; i++ {
			rb := c.HTTPHelper.NewPostRequest(path, &forwardMsg)
			if err := rb.Do(nil); err != nil {
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

func (c *HostOrchestratorServiceImpl) createPolledConnection(device string) (*hoapi.NewConnReply, error) {
	var res hoapi.NewConnReply
	rb := c.HTTPHelper.NewPostRequest("/polled_connections", &hoapi.NewConnMsg{DeviceId: device})
	if err := rb.Do(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *HostOrchestratorServiceImpl) waitForOperation(op *hoapi.Operation, res any) error {
	path := "/operations/" + op.Name + "/:wait"
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusServiceUnavailable},
		NumRetries:  c.WaitRetries,
		RetryDelay:  c.WaitRetryDelay,
	}
	return c.HTTPHelper.NewPostRequest(path, nil).DoWithRetries(res, retryOpts)
}

func (c *HostOrchestratorServiceImpl) FetchArtifacts(req *hoapi.FetchArtifactsRequest, creds string) (*hoapi.FetchArtifactsResponse, error) {
	var op hoapi.Operation
	rb := c.HTTPHelper.NewPostRequest("/artifacts", req)
	if creds != "" {
		rb.AddHeader(c.BuildAPICredentialsHeader, creds)
	}
	if err := rb.Do(&op); err != nil {
		return nil, err
	}

	res := &hoapi.FetchArtifactsResponse{}
	if err := c.waitForOperation(&op, &res); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *HostOrchestratorServiceImpl) CreateCVD(req *hoapi.CreateCVDRequest, creds string) (*hoapi.CreateCVDResponse, error) {
	var op hoapi.Operation
	rb := c.HTTPHelper.NewPostRequest("/cvds", req)
	if creds != "" {
		rb.AddHeader(c.BuildAPICredentialsHeader, creds)
	}
	if err := rb.Do(&op); err != nil {
		return nil, err
	}
	res := &hoapi.CreateCVDResponse{}
	if err := c.waitForOperation(&op, &res); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *HostOrchestratorServiceImpl) ListCVDs() ([]*hoapi.CVD, error) {
	var res hoapi.ListCVDsResponse
	if err := c.HTTPHelper.NewGetRequest("/cvds").Do(&res); err != nil {
		return nil, err
	}
	return res.CVDs, nil
}

func (c *HostOrchestratorServiceImpl) DownloadRuntimeArtifacts(dst io.Writer) error {
	req, err := http.NewRequest("POST", c.HTTPHelper.RootEndpoint+"/runtimeartifacts/:pull", nil)
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

func (c *HostOrchestratorServiceImpl) CreateUploadDir() (string, error) {
	uploadDir := &hoapi.UploadDirectory{}
	if err := c.HTTPHelper.NewPostRequest("/userartifacts", nil).Do(uploadDir); err != nil {
		return "", err
	}
	return uploadDir.Name, nil
}

func (c *HostOrchestratorServiceImpl) UploadFiles(uploadDir string, filenames []string) error {
	return c.UploadFilesWithOptions(uploadDir, filenames, DefaultUploadOptions())
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

func (c *HostOrchestratorServiceImpl) UploadFilesWithOptions(uploadDir string, filenames []string, uploadOpts UploadOptions) error {
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
		Client:        c.HTTPHelper.Client,
		EndpointURL:   c.HTTPHelper.RootEndpoint + "/userartifacts/" + uploadDir,
		DumpOut:       c.HTTPHelper.Dumpster,
		UploadOptions: uploadOpts,
	}
	return uploader.Upload(filenames)
}

func asWebRTCICEServers(in []hoapi.IceServer) []webrtc.ICEServer {
	out := []webrtc.ICEServer{}
	for _, s := range in {
		out = append(out, webrtc.ICEServer{
			URLs: s.URLs,
		})
	}
	return out
}
