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

	"github.com/hashicorp/go-multierror"
)

const (
	// Value to pass as credentials to the Host Orchestrator service endpoints. Any non-empty value is enough.
	InjectedCredentials             = "inject"
	headerNameCOInjectBuildAPICreds = "X-Cutf-Cloud-Orchestrator-Inject-BuildAPI-Creds"
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

func (e *ApiCallError) Is(target error) bool {
	var a *ApiCallError
	return errors.As(target, &a) && *a == *e
}

type ServiceOptions struct {
	RootEndpoint   string
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

	DeleteHosts(names []string) error

	HostService(host string) HostOrchestratorService

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

	// There is a short delay between the creation of the host and the availability of the host
	// orchestrator. This call ensures the host orchestrator had time to start before returning
	// from the this function.
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusBadGateway},
		NumRetries:  3,
		RetryDelay:  5 * time.Second,
	}
	hostPath := fmt.Sprintf("/hosts/%s/", ins.Name)
	if err := c.httpHelper.NewGetRequest(hostPath).DoWithRetries(nil, retryOpts); err != nil {
		return nil, fmt.Errorf("Unable to communicate with host orchestrator: %w", err)
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

func (c *serviceImpl) waitForOperation(op *apiv1.Operation, res any) error {
	path := "/operations/" + op.Name + "/:wait"
	retryOpts := RetryOptions{
		StatusCodes: []int{http.StatusServiceUnavailable},
		NumRetries:  uint(c.ServiceOptions.RetryAttempts),
		RetryDelay:  c.RetryDelay,
	}
	return c.httpHelper.NewPostRequest(path, nil).DoWithRetries(res, retryOpts)
}

func (s *serviceImpl) RootURI() string {
	return s.RootEndpoint
}

func (s *serviceImpl) HostService(host string) HostOrchestratorService {
	hs := &HostOrchestratorServiceImpl{
		HTTPHelper:     s.httpHelper,
		WaitRetries:    uint(s.ServiceOptions.RetryAttempts),
		WaitRetryDelay: s.ServiceOptions.RetryDelay,
		// Make the cloud orchestrator inject the credentials instead
		BuildAPICredentialsHeader: headerNameCOInjectBuildAPICreds,
	}
	hs.HTTPHelper.RootEndpoint = s.httpHelper.RootEndpoint + "/hosts/" + host
	return hs
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
