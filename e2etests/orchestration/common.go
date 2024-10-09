package e2etesting

import (
	"archive/zip"
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
	"strconv"
	"time"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/api/types/container"
	clientpkg "github.com/docker/docker/client"
	"github.com/docker/go-connections/nat"
	orchclient "github.com/google/cloud-android-orchestration/pkg/client"
)

type TestContext struct {
	ServiceURL        string
	DockerImageName   string
	DockerContainerID string
}

// Starts the HO service within a docker container.
func Setup(port int) (*TestContext, error) {
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
	opts := types.ContainerLogsOptions{ShowStdout: true}
	reader, err := h.client.ContainerLogs(ctx, id, opts)
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
	if err := h.client.ContainerRemove(context.TODO(), id, types.ContainerRemoveOptions{Force: true}); err != nil {
		return err
	}
	return nil
}

func (h *DockerHelper) StartADBServer(id, adbBin string) error {
	return h.exec(id, []string{adbBin, "start-server"})
}

func (h *DockerHelper) ConnectADB(id, adbBin, serial string) error {
	return h.exec(id, []string{adbBin, "connect", serial})
}

func (h *DockerHelper) ExecADBShellCommand(id, adbBin, serial string, cmd []string) error {
	return h.exec(id, append([]string{adbBin, "-s", serial, "shell"}, cmd...))
}

type DockerExecExitCodeError struct {
	ExitCode int
}

func (e DockerExecExitCodeError) Error() string {
	return fmt.Sprintf("exit code: %d", e.ExitCode)
}

func (h *DockerHelper) exec(id string, cmd []string) error {
	if err := h.runExec(id, cmd); err != nil {
		return fmt.Errorf("docker exec %v failed: %w", cmd, err)
	}
	return nil
}

func (h *DockerHelper) runExec(id string, cmd []string) error {
	ctx := context.TODO()
	config := types.ExecConfig{
		User:       "root",
		Privileged: true,
		Cmd:        cmd,
	}
	cExec, err := h.client.ContainerExecCreate(ctx, id, config)
	if err != nil {
		return err
	}
	if err = h.client.ContainerExecStart(ctx, cExec.ID, types.ExecStartCheck{}); err != nil {
		return err
	}
	// ContainerExecStart does not block, short poll process status for 60 seconds to
	// check when it has been completed. return a time out error otherwise.
	cExecStatus := types.ContainerExecInspect{}
	for i := 0; i < 30; i++ {
		time.Sleep(500 * time.Millisecond)
		cExecStatus, err = h.client.ContainerExecInspect(ctx, cExec.ID)
		if err != nil {
			return err
		}
		if !cExecStatus.Running {
			break
		}
	}
	if cExecStatus.Running {
		return fmt.Errorf("command %v timed out", cmd)
	}
	if cExecStatus.ExitCode != 0 {
		return &DockerExecExitCodeError{ExitCode: cExecStatus.ExitCode}
	}
	return nil
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

func DownloadHostBugReport(srv orchclient.HostOrchestratorService, group string) error {
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
	if err := srv.CreateBugreport(group, f); err != nil {
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

func UploadAndExtract(srv orchclient.HostOrchestratorService, remoteDir, src string) error {
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
