package common

import (
	"archive/zip"
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"time"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/api/types/container"
	clientpkg "github.com/docker/docker/client"
	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

// Port where the HO service is listening to.
const HOPort = 2080

type TestContext struct {
	ServiceURL        string
	DockerImageName   string
	DockerContainerID string
}

// Starts the HO service within a docker container.
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
	id, err := dockerHelper.RunContainer(img)
	if err != nil {
		return nil, err
	}
	ipAddr, err := dockerHelper.getIpAddr(id)
	if err != nil {
		return nil, err
	}
	result.DockerContainerID = id
	result.ServiceURL = fmt.Sprintf("http://%s:%d", ipAddr, HOPort)
	if err := waitUntilServiceIsUp(result.ServiceURL); err != nil {
		return nil, err
	}
	return result, nil
}

// There's a delay after the container is running and when
// the host orchestrator service is up and running.
func waitUntilServiceIsUp(url string) error {
	waitingTime := 4 * time.Second
	for tries := 0; tries < 3; tries++ {
		time.Sleep(waitingTime)
		if res, err := http.Get(url + "/_debug/statusz"); err == nil && res.StatusCode == http.StatusOK {
			return nil
		}
		waitingTime *= 2
	}
	return errors.New("timeout waiting for service to start")
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
	imgFile, err := os.Open("../../../images+/orchestration-image-dev.tar")
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

func (h *DockerHelper) RunContainer(img string) (string, error) {
	ctx := context.TODO()
	config := &container.Config{
		AttachStdin: true,
		Image:       img,
		Tty:         true,
	}
	hostConfig := &container.HostConfig{
		Privileged: true,
	}
	createRes, err := h.client.ContainerCreate(ctx, config, hostConfig, nil, nil, "")
	if err != nil {
		return "", err
	}
	if err := h.client.ContainerStart(ctx, createRes.ID, container.StartOptions{}); err != nil {
		return "", err
	}
	return createRes.ID, nil
}

func (h *DockerHelper) getIpAddr(id string) (string, error) {
	ctx := context.TODO()
	c, err := h.client.ContainerInspect(ctx, id)
	if err != nil {
		return "", err
	}
	bridgeNetwork := c.NetworkSettings.Networks["bridge"]
	if bridgeNetwork == nil {
		return "", fmt.Errorf("bridge network not found in container: %q", id)
	}
	return bridgeNetwork.IPAddress, nil
}

func (h *DockerHelper) PullLogs(id string) error {
	// `TEST_UNDECLARED_OUTPUTS_DIR` env var is defined by bazel
	// https://bazel.build/reference/test-encyclopedia#initial-conditions
	val, ok := os.LookupEnv("TEST_UNDECLARED_OUTPUTS_DIR")
	if !ok {
		return errors.New("`TEST_UNDECLARED_OUTPUTS_DIR` was not found")
	}
	filename := filepath.Join(val, "container.log")
	f, err := os.Create(filename)
	if err != nil {
		return err
	}
	defer f.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	reader, err := h.client.ContainerLogs(ctx, id, container.LogsOptions{ShowStdout: true})
	if err != nil {
		return err
	}
	_, err = io.Copy(f, reader)
	if err != nil && err != io.EOF {
		return err
	}
	return nil
}

func (h *DockerHelper) RemoveContainer(id string) error {
	if err := h.client.ContainerRemove(context.TODO(), id, container.RemoveOptions{Force: true}); err != nil {
		return err
	}
	return nil
}

func (h *DockerHelper) RemoveHostTool(id, filename string) error {
	_, err := h.exec(id, []string{"rm", filename})
	return err
}

func (h *DockerHelper) StartADBServer(id, adbBin string) error {
	_, err := h.exec(id, []string{adbBin, "start-server"})
	return err
}

func (h *DockerHelper) ConnectADB(id, adbBin, serial string) error {
	_, err := h.exec(id, []string{adbBin, "connect", serial})
	return err
}

// Returns a standard output reader.
func (h *DockerHelper) ExecADBShellCommand(id, adbBin, serial string, cmd []string) (*bufio.Reader, error) {
	return h.exec(id, append([]string{adbBin, "-s", serial, "shell"}, cmd...))
}

type DockerExecExitCodeError struct {
	ExitCode int
}

func (e DockerExecExitCodeError) Error() string {
	return fmt.Sprintf("exit code: %d", e.ExitCode)
}

func (h *DockerHelper) exec(id string, cmd []string) (*bufio.Reader, error) {
	r, err := h.runExec(id, cmd)
	if err != nil {
		return nil, fmt.Errorf("docker exec %v failed: %w", cmd, err)
	}
	return r, nil
}

func (h *DockerHelper) runExec(id string, cmd []string) (*bufio.Reader, error) {
	ctx := context.TODO()
	config := types.ExecConfig{
		User:         "root",
		Privileged:   true,
		Cmd:          cmd,
		AttachStdout: true,
	}
	cExec, err := h.client.ContainerExecCreate(ctx, id, config)
	if err != nil {
		return nil, err
	}
	res, err := h.client.ContainerExecAttach(ctx, cExec.ID, types.ExecStartCheck{})
	if err != nil {
		return nil, err
	}
	// ContainerExecStart does not block, short poll process status for 60 seconds to
	// check when it has been completed. return a time out error otherwise.
	cExecStatus := types.ContainerExecInspect{}
	for i := 0; i < 120; i++ {
		time.Sleep(500 * time.Millisecond)
		cExecStatus, err = h.client.ContainerExecInspect(ctx, cExec.ID)
		if err != nil {
			return nil, err
		}
		if !cExecStatus.Running {
			break
		}
	}
	if cExecStatus.Running {
		return nil, fmt.Errorf("command %v timed out", cmd)
	}
	if cExecStatus.ExitCode != 0 {
		return nil, &DockerExecExitCodeError{ExitCode: cExecStatus.ExitCode}
	}
	return res.Reader, nil
}

func Cleanup(ctx *TestContext) {
	dockerHelper, err := NewDockerHelper()
	if err != nil {
		log.Printf("cleanup: failed creating docker helper: %s\n", err)
		return
	}
	if err := dockerHelper.PullLogs(ctx.DockerContainerID); err != nil {
		log.Printf("cleanup: failed pulling container logs: %s\n", err)
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

func DownloadHostBugReport(srv hoclient.HostOrchestratorService, group string) error {
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
	if err := srv.CreateBugReport(group, hoclient.CreateBugReportOpts{}, f); err != nil {
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}
	if err := verifyZipFile(filename); err != nil {
		return fmt.Errorf("invalid bugreport zip file %s: %w", filename, err)
	}
	return nil
}

func UploadAndExtract(srv hoclient.HostOrchestratorService, remoteDir, src string) error {
	if err := srv.UploadFile(remoteDir, src); err != nil {
		return err
	}
	op, err := srv.ExtractFile(remoteDir, filepath.Base(src))
	if err != nil {
		return err
	}
	if err := srv.WaitForOperation(op.Name, nil); err != nil {
		return err
	}
	return nil
}

// The zip file is verified checking for errors extracting
// each file in the archive.
func verifyZipFile(filename string) error {
	r, err := zip.OpenReader(filename)
	if err != nil {
		return err
	}
	defer r.Close()
	for _, f := range r.File {
		rc, err := f.Open()
		if err != nil {
			return err
		}
		_, readErr := io.Copy(io.Discard, rc)
		rc.Close()
		if readErr != nil {
			return readErr
		}
	}
	return nil
}

func VerifyLogsEndpoint(srvURL, group, name string) error {
	base := fmt.Sprintf("%s/cvds/%s/%s/logs/", srvURL, group, name)
	urls := []string{
		base,
		base + "launcher.log",
		base + "kernel.log",
	}
	for _, v := range urls {
		res, err := http.Get(v)
		if err != nil {
			return err
		}
		if res.StatusCode != http.StatusOK {
			return fmt.Errorf("get %q failed with status code: %d", v, res.StatusCode)
		}
	}
	return nil
}

func CreateCVDFromUserArtifactsDir(srv hoclient.HostOrchestratorService, dir string) (*hoapi.CVD, error) {
	config := `
  {
    "common": {
      "host_package": "@user_artifacts/` + dir + `"
    },
    "instances": [
      {
        "vm": {
          "memory_mb": 8192,
          "setupwizard_mode": "OPTIONAL",
          "cpus": 8
        },
        "disk": {
          "default_build": "@user_artifacts/` + dir + `"
        },
        "streaming": {
          "device_id": "cvd-1"
        }
      }
    ]
  }
  `
	envConfig := make(map[string]interface{})
	if err := json.Unmarshal([]byte(config), &envConfig); err != nil {
		return nil, err
	}
	createReq := &hoapi.CreateCVDRequest{EnvConfig: envConfig}
	res, err := srv.CreateCVD(createReq, &hoclient.AccessTokenBuildAPICreds{})
	if err != nil {
		return nil, fmt.Errorf("failed creating instance: %w", err)
	}
	return res.CVDs[0], nil
}
