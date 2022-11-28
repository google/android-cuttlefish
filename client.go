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
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httputil"
	"net/url"
	"time"

	apiv1 "github.com/google/cloud-android-orchestration/api/v1"
	wclient "github.com/google/cloud-android-orchestration/pkg/webrtcclient"

	"github.com/pion/webrtc/v3"
)

type OpTimeoutError string

func (s OpTimeoutError) Error() string {
	return fmt.Sprintf("waiting for operation %q timed out", string(s))
}

type ApiCallError struct {
	Err *apiv1.Error
}

func (e *ApiCallError) Error() string {
	return fmt.Sprintf("api call error %s: %s", e.Err.Code, e.Err.Message)
}

type APIClient struct {
	client  *http.Client
	baseURL string
	dumpOut io.Writer
	errOut  io.Writer
}

func NewAPIClient(baseURL, proxyURL string, dumpOut, errOut io.Writer) (*APIClient, error) {
	httpClient := &http.Client{}
	// Handles http proxy
	if proxyURL != "" {
		proxyUrl, err := url.Parse(proxyURL)
		if err != nil {
			return nil, err
		}
		httpClient.Transport = &http.Transport{Proxy: http.ProxyURL(proxyUrl)}
	}
	return &APIClient{
		client:  httpClient,
		baseURL: baseURL,
		dumpOut: dumpOut,
		errOut:  errOut,
	}, nil
}

func (c *APIClient) CreateHost(req *apiv1.CreateHostRequest) (*apiv1.HostInstance, error) {
	var op apiv1.Operation
	if err := c.doRequest("POST", "/hosts", req, &op); err != nil {
		return nil, err
	}
	path := "/operations/" + op.Name + "/:wait"
	ins := &apiv1.HostInstance{}
	if err := c.doRequest("POST", path, nil, ins); err != nil {
		return nil, err
	}
	return ins, nil
}

func (c *APIClient) ListHosts() (*apiv1.ListHostsResponse, error) {
	var res apiv1.ListHostsResponse
	if err := c.doRequest("GET", "/hosts", nil, &res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *APIClient) DeleteHost(name string) error {
	path := "/hosts/" + name
	return c.doRequest("DELETE", path, nil, nil)
}

func (c *APIClient) GetInfraConfig(host string) (*apiv1.InfraConfig, error) {
	var res apiv1.InfraConfig
	if err := c.doRequest("GET", fmt.Sprintf("/hosts/%s/infra_config", host), nil, &res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *APIClient) ConnectWebRTC(host, device string, observer wclient.Observer) (*wclient.Connection, error) {
	polledConn, err := c.createPolledConnection(host, device)
	if err != nil {
		return nil, fmt.Errorf("Failed to create polled connection: %w", err)
	}
	infraConfig, err := c.GetInfraConfig(host)
	if err != nil {
		return nil, fmt.Errorf("Failed to obtain infra config: %w", err)
	}
	signaling := c.initHandling(host, polledConn.ConnId, infraConfig.IceServers)
	conn, err := wclient.NewConnection(&signaling, observer)
	if err != nil {
		return nil, fmt.Errorf("Failed to connect to device over webrtc: %w", err)
	}
	return conn, nil
}

func (c *APIClient) createPolledConnection(host, device string) (*apiv1.NewConnReply, error) {
	path := fmt.Sprintf("/hosts/%s/connections", host)
	req := apiv1.NewConnMsg{DeviceId: device}
	var res apiv1.NewConnReply
	if err := c.doRequest("POST", path, &req, &res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *APIClient) initHandling(host, connId string, iceServers []apiv1.IceServer) wclient.Signaling {
	sendCh := make(chan interface{})
	recvCh := make(chan map[string]interface{})

	// The forwarding goroutine will close this channel and stop when the send
	// channel is closed, which will cause the polling go routine to close its own
	// channel and stop as well.
	stopPollCh := make(chan bool)
	go c.webRTCPoll(recvCh, host, connId, stopPollCh)
	go c.webRTCForward(sendCh, host, connId, stopPollCh)

	return wclient.Signaling{
		SendCh:     sendCh,
		RecvCh:     recvCh,
		ICEServers: asWebRTCICEServers(iceServers),
	}
}

const (
	initialPollInterval  = 100 * time.Millisecond
	maxPollInterval      = 2 * time.Second
	maxConsecutiveErrors = 10
)

func (c *APIClient) webRTCPoll(sinkCh chan map[string]interface{}, host, connId string, stopCh chan bool) {
	start := 0
	pollInterval := initialPollInterval
	errCount := 0
	for {
		path := fmt.Sprintf("/hosts/%s/connections/%s/messages?start=%d", host, connId, start)
		var messages []map[string]interface{}
		if err := c.doRequest("GET", path, nil, &messages); err != nil {
			fmt.Fprintf(c.errOut, "Error polling messages: %v\n", err)
			errCount++
			if errCount >= maxConsecutiveErrors {
				fmt.Fprintln(c.errOut, "Reached maximum number of consecutive polling errors, exiting")
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
				fmt.Fprintf(c.errOut, "unexpected message type: %s\n", message["message_type"])
				continue
			}
			sinkCh <- message["payload"].(map[string]interface{})
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

func (c *APIClient) webRTCForward(srcCh chan interface{}, host, connId string, stopPollCh chan bool) {
	for {
		msg, open := <-srcCh
		if !open {
			// The webrtc client closed the channel
			close(stopPollCh)
			break
		}
		forwardMsg := apiv1.ForwardMsg{Payload: msg}
		path := fmt.Sprintf("/hosts/%s/connections/%s/:forward", host, connId)
		i := 0
		for ; i < maxConsecutiveErrors; i++ {
			if err := c.doRequest("POST", path, &forwardMsg, nil); err != nil {
				fmt.Fprintf(c.errOut, "Error sending message to device: %v\n", err)
			} else {
				break
			}
		}
		if i == maxConsecutiveErrors {
			fmt.Fprintln(c.errOut, "Reached maximum number of sending errors, exiting")
			close(stopPollCh)
			return
		}
	}
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

// It either populates the passed response payload reference and returns nil
// error or returns an error. For responses with non-2xx status code an error
// will be returned.
func (c *APIClient) doRequest(method, path string, reqpl, respl interface{}) error {
	var body io.Reader
	if reqpl != nil {
		json, err := json.Marshal(reqpl)
		if err != nil {
			return err
		}
		body = bytes.NewBuffer(json)
	}
	url := c.baseURL + path
	req, err := http.NewRequest(method, url, body)
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	if err := dumpRequest(req, c.dumpOut); err != nil {
		return err
	}
	res, err := c.client.Do(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	if err := dumpResponse(res, c.dumpOut); err != nil {
		return err
	}
	dec := json.NewDecoder(res.Body)
	if res.StatusCode < 200 || res.StatusCode > 299 {
		// DELETE responses do not have a body.
		if method == "DELETE" {
			return &ApiCallError{&apiv1.Error{Message: res.Status}}
		}
		errpl := new(apiv1.Error)
		if err := dec.Decode(errpl); err != nil {
			return err
		}
		return &ApiCallError{errpl}
	}
	if respl != nil {
		if err := dec.Decode(respl); err != nil {
			return err
		}
	}
	return nil
}

func dumpRequest(r *http.Request, w io.Writer) error {
	dump, err := httputil.DumpRequestOut(r, true)
	if err != nil {
		return err
	}
	fmt.Fprintf(w, "%s\n", dump)
	return nil
}

func dumpResponse(r *http.Response, w io.Writer) error {
	dump, err := httputil.DumpResponse(r, true)
	if err != nil {
		return err
	}
	fmt.Fprintf(w, "%s\n", dump)
	return nil
}
