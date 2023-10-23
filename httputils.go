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

type BackOffOpts struct {
	InitialDuration     time.Duration
	RandomizationFactor float64
	Multiplier          float64
	MaxElapsedTime      time.Duration
}

func DefaultChunkUploadBackOffOpts() BackOffOpts {
	return BackOffOpts{
		InitialDuration:     500 * time.Millisecond,
		RandomizationFactor: 0.5,
		Multiplier:          1.5,
		MaxElapsedTime:      2 * time.Minute,
	}
}

type HTTPHelper struct {
	Client                 *http.Client
	RootEndpoint           string
	Retries                uint
	RetryDelay             time.Duration
	ChunkSizeBytes         int64
	ChunkUploadBackOffOpts BackOffOpts
	Dumpster               io.Writer
}

func (h *HTTPHelper) NewGetRequest(path string) *HTTPRequestBuilder {
	return &HTTPRequestBuilder{
		helper: h,
		url:    h.RootEndpoint + path,
		method: "GET",
		Header: make(http.Header),
	}
}

func (h *HTTPHelper) NewDeleteRequest(path string) *HTTPRequestBuilder {
	return &HTTPRequestBuilder{
		helper: h,
		url:    h.RootEndpoint + path,
		method: "DELETE",
		Header: make(http.Header),
	}
}

func (h *HTTPHelper) NewPostRequest(path string, jsonBody any) *HTTPRequestBuilder {
	return &HTTPRequestBuilder{
		helper:   h,
		url:      h.RootEndpoint + path,
		method:   "POST",
		Header:   make(http.Header),
		jsonBody: jsonBody,
	}
}

func (h *HTTPHelper) dumpRequest(r *http.Request) error {
	if h.Dumpster == nil {
		return nil
	}
	dump, err := httputil.DumpRequestOut(r, true)
	if err != nil {
		return fmt.Errorf("Error dumping request: %w", err)
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
		return fmt.Errorf("Error dumping response: %w", err)
	}
	fmt.Fprintf(h.Dumpster, "%s\n", dump)
	return nil
}

type HTTPRequestBuilder struct {
	helper   *HTTPHelper
	url      string
	Header   http.Header
	method   string
	jsonBody any
}

func (rb *HTTPRequestBuilder) Do(ret any) error {
	var body io.Reader
	if rb.jsonBody != nil {
		json, err := json.Marshal(rb.jsonBody)
		if err != nil {
			return fmt.Errorf("Error marshaling request: %w", err)
		}
		body = bytes.NewBuffer(json)
		rb.Header.Set("Content-Type", "application/json")
	}
	req, err := http.NewRequest(rb.method, rb.url, body)
	if err != nil {
		return fmt.Errorf("Error creating request: %w", err)
	}
	for k, v := range rb.Header {
		req.Header[k] = v
	}
	if err := rb.helper.dumpRequest(req); err != nil {
		return err
	}
	res, err := rb.helper.Client.Do(req)
	if err != nil {
		return fmt.Errorf("Error sending request: %w", err)
	}
	for i := uint(0); i < rb.helper.Retries && isRetryableErrorCode(res.StatusCode); i++ {
		err = rb.helper.dumpResponse(res)
		res.Body.Close()
		if err != nil {
			return err
		}
		time.Sleep(rb.helper.RetryDelay)
		if res, err = rb.helper.Client.Do(req); err != nil {
			return fmt.Errorf("Error sending request: %w", err)
		}
	}
	defer res.Body.Close()
	if err := rb.helper.dumpResponse(res); err != nil {
		return err
	}
	dec := json.NewDecoder(res.Body)
	if res.StatusCode < 200 || res.StatusCode > 299 {
		errpl := new(ApiCallError)
		if err := dec.Decode(errpl); err != nil {
			return fmt.Errorf("Error decoding response: %w", err)
		}
		return errpl
	}
	if ret != nil {
		if err := dec.Decode(ret); err != nil {
			return fmt.Errorf("Error decoding response: %w", err)
		}
	}
	return nil
}

type fileInfo struct {
	Name        string
	TotalChunks int
}

type FilesUploader struct {
	Client         *http.Client
	EndpointURL    string
	ChunkSizeBytes int64
	DumpOut        io.Writer
	NumWorkers     int
	BackOffOpts
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
			Context:     ctx,
			Client:      u.Client,
			EndpointURL: u.EndpointURL,
			DumpOut:     u.DumpOut,
			JobsChan:    jobsChan,
			BackOffOpts: u.BackOffOpts,
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
	BackOffOpts
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

func isRetryableErrorCode(code int) bool {
	return code == http.StatusServiceUnavailable ||
		code == http.StatusBadGateway
}
