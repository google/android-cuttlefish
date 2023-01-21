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
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/httptrace"
	"net/http/httputil"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	apiv1 "github.com/google/cloud-android-orchestration/api/v1"
	wclient "github.com/google/cloud-android-orchestration/pkg/webrtcclient"

	hoapi "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
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
	BaseURL        string
	ProxyURL       string
	DumpOut        io.Writer
	ErrOut         io.Writer
	RetryAttempts  int
	RetryDelay     time.Duration
	ChunkSizeBytes int64
}

type Service interface {
	CreateHost(req *apiv1.CreateHostRequest) (*apiv1.HostInstance, error)

	ListHosts() (*apiv1.ListHostsResponse, error)

	DeleteHost(name string) error

	GetInfraConfig(host string) (*apiv1.InfraConfig, error)

	ConnectWebRTC(host, device string, observer wclient.Observer) (*wclient.Connection, error)

	CreateCVD(host string, req *hoapi.CreateCVDRequest) (*hoapi.CVD, error)

	ListCVDs(host string) ([]*hoapi.CVD, error)

	CreateUpload(host string) (string, error)

	UploadFiles(host, uploadDir string, filenames []string) error
}

type serviceImpl struct {
	*ServiceOptions
	client *http.Client
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
		client:         httpClient,
	}, nil
}

func (c *serviceImpl) CreateHost(req *apiv1.CreateHostRequest) (*apiv1.HostInstance, error) {
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

func (c *serviceImpl) ListHosts() (*apiv1.ListHostsResponse, error) {
	var res apiv1.ListHostsResponse
	if err := c.doRequest("GET", "/hosts", nil, &res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *serviceImpl) DeleteHost(name string) error {
	path := "/hosts/" + name
	return c.doRequest("DELETE", path, nil, nil)
}

func (c *serviceImpl) GetInfraConfig(host string) (*apiv1.InfraConfig, error) {
	var res apiv1.InfraConfig
	if err := c.doRequest("GET", fmt.Sprintf("/hosts/%s/infra_config", host), nil, &res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *serviceImpl) ConnectWebRTC(host, device string, observer wclient.Observer) (*wclient.Connection, error) {
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

func (c *serviceImpl) createPolledConnection(host, device string) (*apiv1.NewConnReply, error) {
	path := fmt.Sprintf("/hosts/%s/connections", host)
	req := apiv1.NewConnMsg{DeviceId: device}
	var res apiv1.NewConnReply
	if err := c.doRequest("POST", path, &req, &res); err != nil {
		return nil, err
	}
	return &res, nil
}

func (c *serviceImpl) initHandling(host, connID string, iceServers []apiv1.IceServer) wclient.Signaling {
	sendCh := make(chan any)
	recvCh := make(chan map[string]any)

	// The forwarding goroutine will close this channel and stop when the send
	// channel is closed, which will cause the polling go routine to close its own
	// channel and stop as well.
	stopPollCh := make(chan bool)
	go c.webRTCPoll(recvCh, host, connID, stopPollCh)
	go c.webRTCForward(sendCh, host, connID, stopPollCh)

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

func (c *serviceImpl) webRTCPoll(sinkCh chan map[string]any, host, connID string, stopCh chan bool) {
	start := 0
	pollInterval := initialPollInterval
	errCount := 0
	for {
		path := fmt.Sprintf("/hosts/%s/connections/%s/messages?start=%d", host, connID, start)
		var messages []map[string]any
		if err := c.doRequest("GET", path, nil, &messages); err != nil {
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
		path := fmt.Sprintf("/hosts/%s/connections/%s/:forward", host, connID)
		i := 0
		for ; i < maxConsecutiveErrors; i++ {
			if err := c.doRequest("POST", path, &forwardMsg, nil); err != nil {
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

func asWebRTCICEServers(in []apiv1.IceServer) []webrtc.ICEServer {
	out := []webrtc.ICEServer{}
	for _, s := range in {
		out = append(out, webrtc.ICEServer{
			URLs: s.URLs,
		})
	}
	return out
}

func (c *serviceImpl) CreateCVD(host string, req *hoapi.CreateCVDRequest) (*hoapi.CVD, error) {
	var op hoapi.Operation
	if err := c.doRequest("POST", "/hosts/"+host+"/cvds", req, &op); err != nil {
		return nil, err
	}
	path := "/hosts/" + host + "/operations/" + op.Name + "/:wait"
	cvd := &hoapi.CVD{}
	if err := c.doRequest("POST", path, nil, cvd); err != nil {
		return nil, err
	}
	return cvd, nil
}

func (c *serviceImpl) ListCVDs(host string) ([]*hoapi.CVD, error) {
	var res hoapi.ListCVDsResponse
	if err := c.doRequest("GET", "/hosts/"+host+"/cvds", nil, &res); err != nil {
		return nil, err
	}
	return res.CVDs, nil
}

func (c *serviceImpl) CreateUpload(host string) (string, error) {
	uploadDir := &hoapi.UploadDirectory{}
	if err := c.doRequest("POST", "/hosts/"+host+"/userartifacts", nil, uploadDir); err != nil {
		return "", err
	}
	return uploadDir.Name, nil
}

func (c *serviceImpl) UploadFiles(host, uploadDir string, filenames []string) error {
	if c.ChunkSizeBytes == 0 {
		panic("ChunkSizeBytes value cannot be zero")
	}
	uploader := &filesUploader{
		Client:         c.client,
		EndpointURL:    c.BaseURL + "/hosts/" + host + "/userartifacts/" + uploadDir,
		Filenames:      filenames,
		ChunkSizeBytes: c.ChunkSizeBytes,
		DumpOut:        c.DumpOut,
	}
	return uploader.Upload()
}

// It either populates the passed response payload reference and returns nil
// error or returns an error. For responses with non-2xx status code an error
// will be returned.
func (c *serviceImpl) doRequest(method, path string, reqpl, respl any) error {
	var body io.Reader
	if reqpl != nil {
		json, err := json.Marshal(reqpl)
		if err != nil {
			return fmt.Errorf("Error marshaling request: %w", err)
		}
		body = bytes.NewBuffer(json)
	}
	url := c.BaseURL + path
	req, err := http.NewRequest(method, url, body)
	if err != nil {
		return fmt.Errorf("Error creating request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")
	if err := dumpRequest(req, c.DumpOut); err != nil {
		return fmt.Errorf("Error dumping request: %w", err)
	}
	res, err := c.client.Do(req)
	if err != nil {
		return fmt.Errorf("Error sending request: %w", err)
	}
	for i := 0; i < c.RetryAttempts && (res.StatusCode == http.StatusServiceUnavailable); i++ {
		err = dumpResponse(res, c.DumpOut)
		res.Body.Close()
		if err != nil {
			return fmt.Errorf("Error dumping response: %w", err)
		}
		time.Sleep(c.RetryDelay)
		if res, err = c.client.Do(req); err != nil {
			return fmt.Errorf("Error sending request: %w", err)
		}
	}
	defer res.Body.Close()
	if err := dumpResponse(res, c.DumpOut); err != nil {
		return fmt.Errorf("Error dumping response: %w", err)
	}
	dec := json.NewDecoder(res.Body)
	if res.StatusCode < 200 || res.StatusCode > 299 {
		// DELETE responses do not have a body.
		if method == "DELETE" {
			return &ApiCallError{ErrorMsg: res.Status}
		}
		errpl := new(ApiCallError)
		if err := dec.Decode(errpl); err != nil {
			return fmt.Errorf("Error decoding response: %w", err)
		}
		return errpl
	}
	if respl != nil {
		if err := dec.Decode(respl); err != nil {
			return fmt.Errorf("Error decoding response: %w", err)
		}
	}
	return nil
}

const openConnections = 10

type fileInfo struct {
	Name        string
	TotalChunks int
}

type filesUploader struct {
	Client         *http.Client
	EndpointURL    string
	Filenames      []string
	ChunkSizeBytes int64
	DumpOut        io.Writer
}

func (u *filesUploader) Upload() error {
	infos, err := u.getFilesInfos()
	if err != nil {
		return err
	}
	ctx, cancel := context.WithCancel(context.Background())
	jobsChan := make(chan uploadChunkJob)
	resultsChan := u.startWorkers(ctx, jobsChan)
	go func() {
		defer close(jobsChan)
		u.sendJobs(ctx, jobsChan, infos)
	}()
	// Only first error will be returned.
	var returnErr error
	for err := range resultsChan {
		if returnErr != nil {
			continue
		}
		if err != nil {
			returnErr = err
			cancel()
			// Do not return from here and let the cancellation logic to propagate, resultsChan
			// will be closed eventually.
		}
	}
	return returnErr
}

func (u *filesUploader) getFilesInfos() ([]fileInfo, error) {
	var infos []fileInfo
	for _, name := range u.Filenames {
		stat, err := os.Stat(name)
		if err != nil {
			return nil, err
		}
		info := fileInfo{
			Name:        name,
			TotalChunks: int((stat.Size() + u.ChunkSizeBytes - 1) / u.ChunkSizeBytes),
		}
		infos = append(infos, info)
	}
	return infos, nil
}

func (u *filesUploader) sendJobs(ctx context.Context, jobsChan chan<- uploadChunkJob, infos []fileInfo) {
	for _, info := range infos {
		for i := 0; i < info.TotalChunks; i++ {
			job := uploadChunkJob{
				Filename:       info.Name,
				ChunkNumber:    i + 1,
				TotalChunks:    info.TotalChunks,
				ChunkSizeBytes: u.ChunkSizeBytes,
			}
			select {
			case <-ctx.Done():
				return
			case jobsChan <- job:
				continue
			}
		}
	}
}

func (u *filesUploader) startWorkers(ctx context.Context, jobsChan <-chan uploadChunkJob) <-chan error {
	agg := make(chan error)
	wg := sync.WaitGroup{}
	for i := 0; i < openConnections; i++ {
		wg.Add(1)
		w := uploadChunkWorker{
			Context:     ctx,
			Client:      u.Client,
			EndpointURL: u.EndpointURL,
			DumpOut:     u.DumpOut,
			JobsChan:    jobsChan,
		}
		go func() {
			defer wg.Done()
			ch := w.Start()
			for err := range ch {
				agg <- err
			}
		}()
	}
	go func() {
		wg.Wait()
		close(agg)
	}()
	return agg
}

type uploadChunkJob struct {
	Filename string
	// A number between 1 and `TotalChunks`. The n-th chunk represents a segment of data within the file with size
	// `ChunkSizeBytes` starting the `(n-1) * ChunkSizeBytes`-th byte.
	ChunkNumber    int
	TotalChunks    int
	ChunkSizeBytes int64
}

type uploadChunkWorker struct {
	Context     context.Context
	Client      *http.Client
	EndpointURL string
	DumpOut     io.Writer
	JobsChan    <-chan uploadChunkJob
}

// Returns a channel that will return the result for each of the handled `uploadChunkJob` instances.
func (w *uploadChunkWorker) Start() <-chan error {
	ch := make(chan error)
	go func() {
		defer close(ch)
		for job := range w.JobsChan {
			ch <- w.upload(job)
		}
	}()
	return ch
}

func (w *uploadChunkWorker) upload(job uploadChunkJob) error {
	ctx, cancel := context.WithCancel(w.Context)
	pipeReader, pipeWriter := io.Pipe()
	writer := multipart.NewWriter(pipeWriter)
	go func() {
		defer pipeWriter.Close()
		defer writer.Close()
		if err := writeMultipartRequest(writer, job); err != nil {
			fmt.Fprintf(w.DumpOut, "Error writing multipart request %v", err)
			cancel()
		}
	}()
	// client trace to log whether the request's underlying tcp connection was re-used
	clientTrace := &httptrace.ClientTrace{
		GotConn: func(info httptrace.GotConnInfo) {
			if !info.Reused {
				const msg = "tcp connection was not reused uploading file chunk: %q," +
					"chunk number: %d, chunk total: %d\n"
				fmt.Fprintf(w.DumpOut, msg,
					filepath.Base(job.Filename), job.ChunkNumber, job.TotalChunks)
			}
		},
	}
	traceCtx := httptrace.WithClientTrace(ctx, clientTrace)
	req, err := http.NewRequestWithContext(traceCtx, http.MethodPut, w.EndpointURL, pipeReader)
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", writer.FormDataContentType())
	res, err := w.Client.Do(req)
	if err != nil {
		return err
	}
	if res.StatusCode != 200 {
		const msg = "Failed uploading file chunk with status code %q. " +
			"File %q, chunk number: %d, chunk total: %d."
		return fmt.Errorf(msg, res.Status, filepath.Base(job.Filename), job.ChunkNumber, job.TotalChunks)
	}
	return nil
}

func writeMultipartRequest(writer *multipart.Writer, job uploadChunkJob) error {
	file, err := os.Open(job.Filename)
	if err != nil {
		return err
	}
	defer file.Close()
	if _, err := file.Seek(int64(job.ChunkNumber-1)*job.ChunkSizeBytes, 0); err != nil {
		return err
	}
	if err := addFormField(writer, "chunk_number", strconv.Itoa(job.ChunkNumber)); err != nil {
		return err
	}
	if err := addFormField(writer, "chunk_total", strconv.Itoa(job.TotalChunks)); err != nil {
		return err
	}
	fw, err := writer.CreateFormFile("file", filepath.Base(job.Filename))
	if err != nil {
		return err
	}
	if job.ChunkNumber < job.TotalChunks {
		if _, err = io.CopyN(fw, file, job.ChunkSizeBytes); err != nil {
			return err
		}
	} else {
		if _, err = io.Copy(fw, file); err != nil {
			return err
		}
	}
	return nil
}

func addFormField(writer *multipart.Writer, field, value string) error {
	fw, err := writer.CreateFormField(field)
	if err != nil {
		return err
	}
	_, err = io.Copy(fw, strings.NewReader(value))
	if err != nil {
		return err
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
