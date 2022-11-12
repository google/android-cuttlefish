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

	apiv1 "github.com/google/cloud-android-orchestration/api/v1"
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
}

func NewAPIClient(baseURL, proxyURL string, dumpOut io.Writer) (*APIClient, error) {
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
	}, nil
}

func (c *APIClient) CreateHost() (*apiv1.HostInstance, error) {
	var op apiv1.Operation
	body := apiv1.CreateHostRequest{HostInstance: &apiv1.HostInstance{}}
	if err := c.doRequest("POST", "/hosts", &body, &op); err != nil {
		return nil, err
	}
	path := "/operations/" + op.Name + "/wait"
	if err := c.doRequest("POST", path, nil, &op); err != nil {
		return nil, err
	}
	if op.Result != nil && op.Result.Error != nil {
		err := &ApiCallError{op.Result.Error}
		return nil, err
	}
	if !op.Done {
		return nil, OpTimeoutError(op.Name)
	}
	var ins apiv1.HostInstance
	if err := json.Unmarshal([]byte(op.Result.Response), &ins); err != nil {
		return nil, err
	}
	return &ins, nil
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
