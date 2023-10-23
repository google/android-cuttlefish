// Copyright 2022 Google LLC
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
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"sync"
	"time"

	apiv1 "github.com/google/cloud-android-orchestration/api/v1"
	wclient "github.com/google/cloud-android-orchestration/pkg/webrtcclient"

	hoapi "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/hashicorp/go-multierror"
	"github.com/pion/webrtc/v3"
)

type OpTimeoutError string

func (s OpTimeoutError) Error() string {
	return fmt.Sprintf("waiting for operation %q timed out", string(s))
}

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

func (e *ApiCallError) Is(target error) bool {
	var a *ApiCallError
	return errors.As(target, &a) && *a == *e
}

type ServiceOptions struct {
	RootEndpoint           string
	ProxyURL               string
	DumpOut                io.Writer
	ErrOut                 io.Writer
	RetryAttempts          int
	RetryDelay             time.Duration
	ChunkSizeBytes         int64
	ChunkUploadBackOffOpts BackOffOpts
}

type ConnectWebRTCOpts struct {
	LocalICEConfig *wclient.ICEConfig
}

type Service interface {
	CreateHost(req *apiv1.CreateHostRequest) (*apiv1.HostInstance, error)

	ListHosts() (*apiv1.ListHostsResponse, error)

	DeleteHosts(names []string) error

	GetInfraConfig(host string) (*apiv1.InfraConfig, error)

	ConnectWebRTC(host, device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error)

	FetchArtifacts(host string, req *hoapi.FetchArtifactsRequest) (*hoapi.FetchArtifactsResponse, error)

	CreateCVD(host string, req *hoapi.CreateCVDRequest) (*hoapi.CreateCVDResponse, error)

	ListCVDs(host string) ([]*hoapi.CVD, error)

	// Downloads runtime artifacts tar file from passed `host` into `dst` filename.
	DownloadRuntimeArtifacts(host string, dst io.Writer) error

	CreateUpload(host string) (string, error)

	UploadFiles(host, uploadDir string, filenames []string) error

	RootURI() string
}

type serviceImpl struct {
	*ServiceOptions
	httpHelper HTTPHelper
}

type ServiceBuilder func(opts *ServiceOptions) (Service, error)

func NewService(opts *ServiceOptions) (Service, error) {
	httpClient := &http.Client{}
	// Handles http proxy
	if opts.ProxyURL != "" {
		proxyUrl, err := url.Parse(opts.ProxyURL)
		if err != nil {
			return nil, err
		}
		httpClient.Transport = &http.Transport{Proxy: http.ProxyURL(proxyUrl)}
	}
	return &serviceImpl{
		ServiceOptions: opts,
		httpHelper: HTTPHelper{
			Client:       httpClient,
			RootEndpoint: opts.RootEndpoint,
			Retries:      uint(opts.RetryAttempts),
			RetryDelay:   opts.RetryDelay,
			Dumpster:     opts.DumpOut,
		},
	}, nil
}

func (c *serviceImpl) CreateHost(req *apiv1.CreateHostRequest) (*apiv1.HostInstance, error) {
	var op apiv1.Operation
	if err := c.httpHelper.NewPostRequest("/hosts", req).Do(&op); err != nil {
		return nil, err
	}
	ins := &apiv1.HostInstance{}
	if err := c.waitForOperation(&op, ins); err != nil {
		return nil, err
	}
	return ins, nil
}

func (c *serviceImpl) ListHosts() (*apiv1.ListHostsResponse, error) {
	var res apiv1.ListHostsResponse
	if err := c.httpHelper.NewGetRequest("/hosts").Do(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *serviceImpl) DeleteHosts(names []string) error {
	var wg sync.WaitGroup
	var mu sync.Mutex
	var merr error
	for _, name := range names {
		wg.Add(1)
		go func(name string) {
			defer wg.Done()
			if err := c.httpHelper.NewDeleteRequest("/hosts/" + name).Do(nil); err != nil {
				mu.Lock()
				defer mu.Unlock()
				merr = multierror.Append(merr, fmt.Errorf("Delete host %q failed: %w", name, err))
			}
		}(name)
	}
	wg.Wait()
	return merr
}

func (c *serviceImpl) GetInfraConfig(host string) (*apiv1.InfraConfig, error) {
	var res apiv1.InfraConfig
	if err := c.httpHelper.NewGetRequest(fmt.Sprintf("/hosts/%s/infra_config", host)).Do(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *serviceImpl) ConnectWebRTC(host, device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error) {
	polledConn, err := c.createPolledConnection(host, device)
	if err != nil {
		return nil, fmt.Errorf("Failed to create polled connection: %w", err)
	}
	iceServers := []webrtc.ICEServer{}
	if opts.LocalICEConfig != nil {
		iceServers = append(iceServers, opts.LocalICEConfig.ICEServers...)
	}
	infraConfig, err := c.GetInfraConfig(host)
	if err != nil {
		return nil, fmt.Errorf("Failed to obtain infra config: %w", err)
	}
	iceServers = append(iceServers, asWebRTCICEServers(infraConfig.IceServers)...)
	signaling := c.initHandling(host, polledConn.ConnId, iceServers)
	conn, err := wclient.NewConnectionWithLogger(&signaling, observer, logger)
	if err != nil {
		return nil, fmt.Errorf("Failed to connect to device over webrtc: %w", err)
	}
	return conn, nil
}

func (c *serviceImpl) createPolledConnection(host, device string) (*apiv1.NewConnReply, error) {
	path := fmt.Sprintf("/hosts/%s/polled_connections", host)
	req := apiv1.NewConnMsg{DeviceId: device}
	var res apiv1.NewConnReply
	if err := c.httpHelper.NewPostRequest(path, &req).Do(&res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *serviceImpl) initHandling(host, connID string, iceServers []webrtc.ICEServer) wclient.Signaling {
	sendCh := make(chan any)
	recvCh := make(chan map[string]any)

	// The forwarding goroutine will close this channel and stop when the send
	// channel is closed, which will cause the polling go routine to close its own
	// channel and stop as well.
	stopPollCh := make(chan bool)
	go c.webRTCPoll(recvCh, host, connID, stopPollCh)
	go c.webRTCForward(sendCh, host, connID, stopPollCh)

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

func (c *serviceImpl) webRTCPoll(sinkCh chan map[string]any, host, connID string, stopCh chan bool) {
	start := 0
	pollInterval := initialPollInterval
	errCount := 0
	for {
		path := fmt.Sprintf("/hosts/%s/polled_connections/%s/messages?start=%d", host, connID, start)
		var messages []map[string]any
		if err := c.httpHelper.NewGetRequest(path).Do(&messages); err != nil {
			fmt.Fprintf(c.ErrOut, "Error polling messages: %v\n", err)
			errCount++
			if errCount >= maxConsecutiveErrors {
				fmt.Fprintln(c.ErrOut, "Reached maximum number of consecutive polling errors, exiting")
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
				fmt.Fprintf(c.ErrOut, "unexpected message type: %s\n", message["message_type"])
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

func (c *serviceImpl) webRTCForward(srcCh chan any, host, connID string, stopPollCh chan bool) {
	for {
		msg, open := <-srcCh
		if !open {
			// The webrtc client closed the channel
			close(stopPollCh)
			break
		}
		forwardMsg := apiv1.ForwardMsg{Payload: msg}
		path := fmt.Sprintf("/hosts/%s/polled_connections/%s/:forward", host, connID)
		i := 0
		for ; i < maxConsecutiveErrors; i++ {
			if err := c.httpHelper.NewPostRequest(path, &forwardMsg).Do(nil); err != nil {
				fmt.Fprintf(c.ErrOut, "Error sending message to device: %v\n", err)
			} else {
				break
			}
		}
		if i == maxConsecutiveErrors {
			fmt.Fprintln(c.ErrOut, "Reached maximum number of sending errors, exiting")
			close(stopPollCh)
			return
		}
	}
}

func (c *serviceImpl) waitForOperation(op *apiv1.Operation, res any) error {
	path := "/operations/" + op.Name + "/:wait"
	return c.httpHelper.NewPostRequest(path, nil).Do(res)
}

func (c *serviceImpl) waitForHostOperation(host string, op *hoapi.Operation, res any) error {
	path := "/hosts/" + host + "/operations/" + op.Name + "/:wait"
	return c.httpHelper.NewPostRequest(path, nil).Do(res)
}

func asWebRTCICEServers(in []apiv1.IceServer) []webrtc.ICEServer {
	out := []webrtc.ICEServer{}
	for _, s := range in {
		out = append(out, webrtc.ICEServer{
			URLs: s.URLs,
		})
	}
	return out
}

const headerNameCOInjectBuildAPICreds = "X-Cutf-Cloud-Orchestrator-Inject-BuildAPI-Creds"

func (c *serviceImpl) FetchArtifacts(
	host string, req *hoapi.FetchArtifactsRequest) (*hoapi.FetchArtifactsResponse, error) {
	var op hoapi.Operation
	path := "/hosts/" + host + "/artifacts"
	rb := c.httpHelper.NewPostRequest(path, req)
	// Cloud Orchestrator only checks for the presence of the header, hence an empty string value is ok.
	rb.Header.Add(headerNameCOInjectBuildAPICreds, "")
	if err := rb.Do(&op); err != nil {
		return nil, err
	}

	res := &hoapi.FetchArtifactsResponse{}
	if err := c.waitForHostOperation(host, &op, &res); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *serviceImpl) CreateCVD(host string, req *hoapi.CreateCVDRequest) (*hoapi.CreateCVDResponse, error) {
	var op hoapi.Operation
	rb := c.httpHelper.NewPostRequest("/hosts/"+host+"/cvds", req)
	// Cloud Orchestrator only checks for the existence of the header, hence an empty string value is ok.
	rb.Header.Add(headerNameCOInjectBuildAPICreds, "")
	if err := rb.Do(&op); err != nil {
		return nil, err
	}
	res := &hoapi.CreateCVDResponse{}
	if err := c.waitForHostOperation(host, &op, &res); err != nil {
		return nil, err
	}
	return res, nil
}

func (c *serviceImpl) ListCVDs(host string) ([]*hoapi.CVD, error) {
	var res hoapi.ListCVDsResponse
	if err := c.httpHelper.NewGetRequest("/hosts/" + host + "/cvds").Do(&res); err != nil {
		return nil, err
	}
	return res.CVDs, nil
}

func (c *serviceImpl) DownloadRuntimeArtifacts(host string, dst io.Writer) error {
	req, err := http.NewRequest("POST", c.RootEndpoint+"/hosts/"+host+"/runtimeartifacts/:pull", nil)
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

func (c *serviceImpl) CreateUpload(host string) (string, error) {
	uploadDir := &hoapi.UploadDirectory{}
	if err := c.httpHelper.NewPostRequest("/hosts/"+host+"/userartifacts", nil).Do(uploadDir); err != nil {
		return "", err
	}
	return uploadDir.Name, nil
}

const openConnections = 32

func (c *serviceImpl) UploadFiles(host, uploadDir string, filenames []string) error {
	if c.ChunkSizeBytes == 0 {
		panic("ChunkSizeBytes value cannot be zero")
	}
	uploader := &FilesUploader{
		Client:         c.httpHelper.Client,
		EndpointURL:    c.RootEndpoint + "/hosts/" + host + "/userartifacts/" + uploadDir,
		ChunkSizeBytes: c.ChunkSizeBytes,
		DumpOut:        c.DumpOut,
		NumWorkers:     openConnections,
		BackOffOpts:    c.ChunkUploadBackOffOpts,
	}
	return uploader.Upload(filenames)
}

func (s *serviceImpl) RootURI() string {
	return s.RootEndpoint
}

func BuildRootEndpoint(serviceURL, version, zone string) string {
	result := serviceURL + "/" + version
	if zone != "" {
		result += "/zones/" + zone
	}
	return result
}

func BuilHostIndexURL(rootEndpoint, host string) string {
	return fmt.Sprintf("%s/hosts/%s/", rootEndpoint, host)
}

func BuildCVDLogsURL(rootEndpoint, host, cvd string) string {
	return fmt.Sprintf("%s/hosts/%s/cvds/%s/logs/", rootEndpoint, host, cvd)
}
