// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package orchestration

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"testing"
	"time"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/api/types/container"
	clientpkg "github.com/docker/docker/client"
	"github.com/docker/go-connections/nat"
	hoapi "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	orchclient "github.com/google/cloud-android-orchestration/pkg/client"
	"github.com/google/go-cmp/cmp"
)

func TestCreateSingleInstance(t *testing.T) {
	ctx, err := Setup()
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		Cleanup(ctx)
	})
  buildID := os.Getenv("BUILD_ID")
  buildTarget := os.Getenv("BUILD_TARGET")
	srv := orchclient.NewHostOrchestratorService(ctx.ServiceURL)
	createReq := &hoapi.CreateCVDRequest{
		CVD: &hoapi.CVD{
			BuildSource: &hoapi.BuildSource{
				AndroidCIBuildSource: &hoapi.AndroidCIBuildSource{
					MainBuild: &hoapi.AndroidCIBuild{
						BuildID: buildID,
						Target:  buildTarget,
					},
				},
			},
		},
	}

	got, err := srv.CreateCVD(createReq, "")

	if err != nil {
		t.Fatal(err)
	}
	want := &hoapi.CreateCVDResponse{
		CVDs: []*hoapi.CVD{
			&hoapi.CVD{
				Group:          "cvd",
				Name:           "1",
				BuildSource:    &hoapi.BuildSource{},
				Status:         "Running",
				Displays:       []string{"720 x 1280 ( 320 )"},
				WebRTCDeviceID: "cvd-1",
			},
		},
	}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

type TestContext struct {
	ServiceURL        string
	DockerImageName   string
	DockerContainerID string
}

func Setup() (*TestContext, error) {
	result := &TestContext{}
	dockerHelper, err := NewDockerHelper()
	if err != nil {
		return nil, err
	}
	img, err := dockerHelper.LoadImage()
	if err != nil {
		return nil, err
	}
	result.DockerImageName = img
	const port = 61000
	id, err := dockerHelper.RunContainer(img, port)
	if err != nil {
		return nil, err
	}
	result.DockerContainerID = id
	result.ServiceURL = fmt.Sprintf("http://0.0.0.0:%d", port)
	if err := waitUntilServiceIsUp(result.ServiceURL); err != nil {
		return nil, err
	}
	return result, nil
}

// There's a delay after the container is running and when
// the host orchestrator service is up and running.
func waitUntilServiceIsUp(url string) error {
	waitSecs := 4 * time.Second
	for tries := 0; tries < 3; tries++ {
		time.Sleep(waitSecs)
		if res, err := http.Get(url + "/_debug/statusz"); err == nil && res.StatusCode == http.StatusOK {
      return nil
		} 
		waitSecs *= 2
	}
	return errors.New("timeout waiting for service to start")
}

func DownloadHostBugReport(ctx *TestContext) error {
	// `TEST_UNDECLARED_OUTPUTS_DIR` env var is defined by bazel
	// https://bazel.build/reference/test-encyclopedia#initial-conditions
	val, ok := os.LookupEnv("TEST_UNDECLARED_OUTPUTS_DIR")
	if !ok {
		return errors.New("`TEST_UNDECLARED_OUTPUTS_DIR` was not found")
	}
	filename := filepath.Join(val, "host_bugreport.zip")
	f, err := os.Create(filename)
	if err != nil {
		return err
	}
	srv := orchclient.NewHostOrchestratorService(ctx.ServiceURL)
	if err := srv.DownloadRuntimeArtifacts(f); err != nil {
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}
	return nil
}

func Cleanup(ctx *TestContext) {
	if err := DownloadHostBugReport(ctx); err != nil {
		log.Printf("cleanup: failed downloading host_bugreport.zip: %s\n", err)
	}
	dockerHelper, err := NewDockerHelper()
	if err != nil {
		log.Printf("cleanup: failed creating docker helper: %s\n", err)
		return
	}
	if err := dockerHelper.RemoveContainer(ctx.DockerContainerID); err != nil {
		log.Printf("cleanup: failed removing container: %s\n", err)
		return
	}
	if err := dockerHelper.RemoveImage(ctx.DockerImageName); err != nil {
		log.Printf("cleanup: failed removing image: %s\n", err)
		return
	}
}

type DockerHelper struct {
	client *clientpkg.Client
}

func NewDockerHelper() (*DockerHelper, error) {
	client, err := clientpkg.NewClientWithOpts(clientpkg.FromEnv)
	if err != nil {
		return nil, err
	}
	return &DockerHelper{client: client}, nil
}

func (h *DockerHelper) LoadImage() (string, error) {
	imgFile, err := os.Open("../external/images/docker/orchestration-image.tar")
	if err != nil {
		return "", err
	}
	loadRes, err := h.client.ImageLoad(context.TODO(), imgFile, true)
	if err != nil {
		return "", err
	}
	defer loadRes.Body.Close()
	decoder := json.NewDecoder(loadRes.Body)
	loadResBody := struct {
		Stream string `json:"stream"`
	}{}
	if err := decoder.Decode(&loadResBody); err != nil {
		return "", err
	}
	re := regexp.MustCompile(`Loaded image: (.*)`)
	matches := re.FindStringSubmatch(loadResBody.Stream)
	if len(matches) != 2 {
		return "", fmt.Errorf("unexpected load image response: %q", loadResBody.Stream)
	}
	return matches[1], nil
}

func (h *DockerHelper) RemoveImage(name string) error {
	if _, err := h.client.ImageRemove(context.TODO(), name, types.ImageRemoveOptions{}); err != nil {
		return err
	}
	return nil
}

func (h *DockerHelper) RunContainer(img string, hostPort int) (string, error) {
	ctx := context.TODO()
	config := &container.Config{
		AttachStdin: true,
		Image:       img,
		Tty:         true,
	}
	hostConfig := &container.HostConfig{
		PortBindings: nat.PortMap{"2080/tcp": []nat.PortBinding{{HostPort: strconv.Itoa(hostPort)}}},
		Privileged:   true,
	}
	createRes, err := h.client.ContainerCreate(ctx, config, hostConfig, nil, nil, "")
	if err != nil {
		return "", err
	}
	if err := h.client.ContainerStart(ctx, createRes.ID, types.ContainerStartOptions{}); err != nil {
		return "", err
	}
	return createRes.ID, nil
}

func (h *DockerHelper) RemoveContainer(id string) error {
	if err := h.client.ContainerRemove(context.TODO(), id, types.ContainerRemoveOptions{Force: true}); err != nil {
		return err
	}
	return nil
}
