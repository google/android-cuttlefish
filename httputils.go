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
	"fmt"
	"github.com/cenkalti/backoff/v4"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/httptrace"
	"net/http/httputil"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

type HTTPHelper struct {
	Client       *http.Client
	RootEndpoint string
	Dumpster     io.Writer
}

func (h *HTTPHelper) NewGetRequest(path string) *HTTPRequestBuilder {
	req, err := http.NewRequest(http.MethodGet, h.RootEndpoint+path, nil)
	return &HTTPRequestBuilder{
		helper:  h,
		request: req,
		err:     err,
	}
}

func (h *HTTPHelper) NewDeleteRequest(path string) *HTTPRequestBuilder {
	req, err := http.NewRequest(http.MethodDelete, h.RootEndpoint+path, nil)
	return &HTTPRequestBuilder{
		helper:  h,
		request: req,
		err:     err,
	}
}

func (h *HTTPHelper) NewPostRequest(path string, jsonBody any) *HTTPRequestBuilder {
	body := []byte{}
	var err error
	if jsonBody != nil {
		if body, err = json.Marshal(jsonBody); err != nil {
			return &HTTPRequestBuilder{helper: h, request: nil, err: err}
		}
	}
	var req *http.Request
	if req, err = http.NewRequest(http.MethodPost, h.RootEndpoint+path, bytes.NewBuffer(body)); err != nil {
		return &HTTPRequestBuilder{helper: h, request: nil, err: err}
	}
	req.Header.Set("Content-Type", "application/json")
	return &HTTPRequestBuilder{
		helper:  h,
		request: req,
		err:     err,
	}
}

func (h *HTTPHelper) dumpRequest(r *http.Request) error {
	if h.Dumpster == nil {
		return nil
	}
	dump, err := httputil.DumpRequestOut(r, true)
	if err != nil {
		return fmt.Errorf("error dumping request: %w", err)
	}
	fmt.Fprintf(h.Dumpster, "%s\n", dump)
	return nil
}

func (h *HTTPHelper) dumpResponse(r *http.Response) error {
	if h.Dumpster == nil {
		return nil
	}
	dump, err := httputil.DumpResponse(r, true)
	if err != nil {
		return fmt.Errorf("error dumping response: %w", err)
	}
	fmt.Fprintf(h.Dumpster, "%s\n", dump)
	return nil
}

type HTTPRequestBuilder struct {
	helper  *HTTPHelper
	request *http.Request
	err     error
}

func (rb *HTTPRequestBuilder) AddHeader(key, value string) {
	if rb.request == nil {
		return
	}
	rb.request.Header.Add(key, value)
}

func (rb *HTTPRequestBuilder) SetHeader(key, value string) {
	if rb.request == nil {
		return
	}
	rb.request.Header.Set(key, value)
}

type RetryOptions struct {
	StatusCodes []int
	NumRetries  uint
	RetryDelay  time.Duration
}

func (rb *HTTPRequestBuilder) Do(ret any) error {
	return rb.DoWithRetries(ret, RetryOptions{})
}

func (rb *HTTPRequestBuilder) DoWithRetries(ret any, retryOpts RetryOptions) error {
	if rb.err != nil {
		return rb.err
	}
	if err := rb.helper.dumpRequest(rb.request); err != nil {
		return err
	}
	res, err := rb.helper.Client.Do(rb.request)
	if err != nil {
		return fmt.Errorf("error sending request: %w", err)
	}
	for i := uint(0); i < retryOpts.NumRetries && isIn(res.StatusCode, retryOpts.StatusCodes); i++ {
		err = rb.helper.dumpResponse(res)
		res.Body.Close()
		if err != nil {
			return err
		}
		time.Sleep(retryOpts.RetryDelay)
		if res, err = rb.helper.Client.Do(rb.request); err != nil {
			return fmt.Errorf("error sending request: %w", err)
		}
	}
	defer res.Body.Close()
	if err := rb.helper.dumpResponse(res); err != nil {
		return err
	}
	return rb.parseResponse(res, ret)
}

func (rb *HTTPRequestBuilder) parseResponse(res *http.Response, ret any) error {
	dec := json.NewDecoder(res.Body)
	if res.StatusCode < 200 || res.StatusCode > 299 {
		errpl := new(ApiCallError)
		if err := dec.Decode(errpl); err != nil {
			return fmt.Errorf("error decoding response: %w", err)
		}
		return errpl
	}
	if ret != nil {
		if err := dec.Decode(ret); err != nil {
			return fmt.Errorf("error decoding response: %w", err)
		}
	}
	return nil
}

// Ideally this would use slices.Contains, but it needs to build with an older go version.
func isIn(code int, codes []int) bool {
	for _, c := range codes {
		if c == code {
			return true
		}
	}
	return false
}

type fileInfo struct {
	Name        string
	TotalChunks int
}

type ExpBackOffOptions struct {
	InitialDuration     time.Duration
	RandomizationFactor float64
	Multiplier          float64
	MaxElapsedTime      time.Duration
}

type UploadOptions struct {
	BackOffOpts    ExpBackOffOptions
	ChunkSizeBytes int64
	NumWorkers     int
}

type FilesUploader struct {
	Client      *http.Client
	EndpointURL string
	DumpOut     io.Writer
	UploadOptions
}

func (u *FilesUploader) Upload(files []string) error {
	infos, err := u.getFilesInfos(files)
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
		if err != nil {
			fmt.Fprintf(u.DumpOut, "Error uploading file chunk: %v\n", err)
			if returnErr == nil {
				returnErr = err
				cancel()
				// Do not return from here and let the cancellation logic to propagate, resultsChan
				// will be closed eventually.
			}
		}
	}
	return returnErr
}

func (u *FilesUploader) getFilesInfos(files []string) ([]fileInfo, error) {
	var infos []fileInfo
	for _, name := range files {
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

func (u *FilesUploader) sendJobs(ctx context.Context, jobsChan chan<- uploadChunkJob, infos []fileInfo) {
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

func (u *FilesUploader) startWorkers(ctx context.Context, jobsChan <-chan uploadChunkJob) <-chan error {
	agg := make(chan error)
	wg := sync.WaitGroup{}
	for i := 0; i < u.NumWorkers; i++ {
		wg.Add(1)
		w := uploadChunkWorker{
			Context:       ctx,
			Client:        u.Client,
			EndpointURL:   u.EndpointURL,
			DumpOut:       u.DumpOut,
			JobsChan:      jobsChan,
			UploadOptions: u.UploadOptions,
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
	UploadOptions
}

// Returns a channel that will return the result for each of the handled `uploadChunkJob` instances.
func (w *uploadChunkWorker) Start() <-chan error {
	ch := make(chan error)
	b := backoff.NewExponentialBackOff()
	b.InitialInterval = w.BackOffOpts.InitialDuration
	b.RandomizationFactor = w.BackOffOpts.RandomizationFactor
	b.Multiplier = w.BackOffOpts.Multiplier
	b.MaxElapsedTime = w.BackOffOpts.MaxElapsedTime
	b.Reset()
	go func() {
		defer close(ch)
		for job := range w.JobsChan {
			var err error
			for {
				err = w.upload(job)
				if err == nil {
					b.Reset()
					break
				}
				duration := b.NextBackOff()
				if duration == backoff.Stop {
					break
				} else {
					time.Sleep(duration)
				}
			}
			ch <- err
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
		const msg = "failed uploading file chunk with status code %q. " +
			"File %q, chunk number: %d, chunk total: %d"
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
	if err := addFormField(writer, "chunk_size_bytes", strconv.FormatInt(job.ChunkSizeBytes, 10)); err != nil {
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
