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

	// Create a new device with artifacts from the build server or previously uploaded by the user.
	CreateCVD(req *hoapi.CreateCVDRequest) (*hoapi.CreateCVDResponse, error)

	FetchArtifacts(req *hoapi.FetchArtifactsRequest) (*hoapi.FetchArtifactsResponse, error)

	// Downloads runtime artifacts tar file into `dst`.
	DownloadRuntimeArtifacts(dst io.Writer) error

	// Creates a webRTC connection to a device running in this host.
	ConnectWebRTC(device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error)
}

type hostOrchestratorServiceImpl struct {
	httpHelper             HTTPHelper
	ChunkSizeBytes         int64
	ChunkUploadBackOffOpts BackOffOpts
}

func (c *hostOrchestratorServiceImpl) getInfraConfig() (*hoapi.InfraConfig, error) {
	var res hoapi.InfraConfig
	if err := c.httpHelper.NewGetRequest(fmt.Sprintf("/infra_config")).Do(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *hostOrchestratorServiceImpl) ConnectWebRTC(device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error) {
	polledConn, err := c.createPolledConnection(device)
	if err != nil {
		return nil, fmt.Errorf("Failed to create polled connection: %w", err)
	}
	iceServers := []webrtc.ICEServer{}
	if opts.LocalICEConfig != nil {
		iceServers = append(iceServers, opts.LocalICEConfig.ICEServers...)
	}
	infraConfig, err := c.getInfraConfig()
	if err != nil {
		return nil, fmt.Errorf("Failed to obtain infra config: %w", err)
	}
	iceServers = append(iceServers, asWebRTCICEServers(infraConfig.IceServers)...)
	signaling := c.initHandling(polledConn.ConnId, iceServers, logger)
	conn, err := wclient.NewConnectionWithLogger(&signaling, observer, logger)
	if err != nil {
		return nil, fmt.Errorf("Failed to connect to device over webrtc: %w", err)
	}
	return conn, nil
}

func (c *hostOrchestratorServiceImpl) initHandling(connID string, iceServers []webrtc.ICEServer, logger io.Writer) wclient.Signaling {
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

func (c *hostOrchestratorServiceImpl) webRTCPoll(sinkCh chan map[string]any, connID string, stopCh chan bool, logger io.Writer) {
	start := 0
	pollInterval := initialPollInterval
	errCount := 0
	for {
		path := fmt.Sprintf("/polled_connections/%s/messages?start=%d", connID, start)
		var messages []map[string]any
		if err := c.httpHelper.NewGetRequest(path).Do(&messages); err != nil {
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
		case _, _ = <-stopCh:
			// The forwarding goroutine has requested a stop
			close(sinkCh)
			return
		case <-time.After(pollInterval):
			// poll for messages again
		}
	}
}

func (c *hostOrchestratorServiceImpl) webRTCForward(srcCh chan any, connID string, stopPollCh chan bool, logger io.Writer) {
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
			rb := c.httpHelper.NewPostRequest(path, &forwardMsg)
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

func (c *hostOrchestratorServiceImpl) createPolledConnection(device string) (*hoapi.NewConnReply, error) {
	var res hoapi.NewConnReply
	rb := c.httpHelper.NewPostRequest("/polled_connections", &hoapi.NewConnMsg{DeviceId: device})
	if err := rb.Do(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *hostOrchestratorServiceImpl) waitForOperation(op *hoapi.Operation, res any) error {
	path := "/operations/" + op.Name + "/:wait"
	return c.httpHelper.NewPostRequest(path, nil).Do(res)
}

func (c *hostOrchestratorServiceImpl) FetchArtifacts(req *hoapi.FetchArtifactsRequest) (*hoapi.FetchArtifactsResponse, error) {
	var op hoapi.Operation
	rb := c.httpHelper.NewPostRequest("/artifacts", req)
	// Cloud Orchestrator only checks for the presence of the header, hence an empty string value is ok.
	rb.Header.Add(headerNameCOInjectBuildAPICreds, "")
	if err := rb.Do(&op); err != nil {
		return nil, err
	}

	res := &hoapi.FetchArtifactsResponse{}
	if err := c.waitForOperation(&op, &res); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *hostOrchestratorServiceImpl) CreateCVD(req *hoapi.CreateCVDRequest) (*hoapi.CreateCVDResponse, error) {
	var op hoapi.Operation
	rb := c.httpHelper.NewPostRequest("/cvds", req)
	// Cloud Orchestrator only checks for the existence of the header, hence an empty string value is ok.
	rb.Header.Add(headerNameCOInjectBuildAPICreds, "")
	if err := rb.Do(&op); err != nil {
		return nil, err
	}
	res := &hoapi.CreateCVDResponse{}
	if err := c.waitForOperation(&op, &res); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *hostOrchestratorServiceImpl) ListCVDs() ([]*hoapi.CVD, error) {
	var res hoapi.ListCVDsResponse
	if err := c.httpHelper.NewGetRequest("/cvds").Do(&res); err != nil {
		return nil, err
	}
	return res.CVDs, nil
}

func (c *hostOrchestratorServiceImpl) DownloadRuntimeArtifacts(dst io.Writer) error {
	req, err := http.NewRequest("POST", c.httpHelper.RootEndpoint+"/runtimeartifacts/:pull", nil)
	if err != nil {
		return err
	}
	res, err := c.httpHelper.Client.Do(req)
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

func (c *hostOrchestratorServiceImpl) CreateUploadDir() (string, error) {
	uploadDir := &hoapi.UploadDirectory{}
	if err := c.httpHelper.NewPostRequest("/userartifacts", nil).Do(uploadDir); err != nil {
		return "", err
	}
	return uploadDir.Name, nil
}

const openConnections = 32

func (c *hostOrchestratorServiceImpl) UploadFiles(uploadDir string, filenames []string) error {
	if c.ChunkSizeBytes == 0 {
		panic("ChunkSizeBytes value cannot be zero")
	}
	uploader := &FilesUploader{
		Client:         c.httpHelper.Client,
		EndpointURL:    c.httpHelper.RootEndpoint + "/userartifacts/" + uploadDir,
		ChunkSizeBytes: c.ChunkSizeBytes,
		DumpOut:        c.httpHelper.Dumpster,
		NumWorkers:     openConnections,
		BackOffOpts:    c.ChunkUploadBackOffOpts,
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
