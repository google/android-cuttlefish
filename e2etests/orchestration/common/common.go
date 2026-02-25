package common

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
	"os/exec"
	"path/filepath"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

func DownloadHostBugReport(srv hoclient.HostOrchestratorClient, group string) error {
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

func PrepareArtifact(srv hoclient.HostOrchestratorClient, filename string) (string, error) {
	if err := srv.UploadArtifact(filename); err != nil {
		return "", err
	}
	if err := srv.ExtractArtifact(filename); err != nil {
		return "", err
	}
	res, err := srv.CreateImageDirectory()
	if err != nil {
		return "", err
	}
	if err := srv.UpdateImageDirectoryWithUserArtifact(res.ID, filename); err != nil {
		return "", err
	}
	return res.ID, nil
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

func CreateCVDFromImageDirs(srv hoclient.HostOrchestratorClient, hostPkgDir, imageDir string) (*hoapi.CVD, error) {
	config := `
  {
    "common": {
      "host_package": "@image_dirs/` + hostPkgDir + `"
    },
    "instances": [
      {
        "vm": {
          "memory_mb": 8192,
          "setupwizard_mode": "OPTIONAL",
          "cpus": 8
        },
        "disk": {
          "default_build": "@image_dirs/` + imageDir + `"
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

type AdbHelper struct {
	bin string
}

func NewAdbHelper() *AdbHelper {
	return &AdbHelper{
		bin: "/usr/bin/adb",
	}
}

func (h *AdbHelper) StartServer() error {
	_, err := runCmd(h.bin, "start-server")
	return err
}

func (h *AdbHelper) Connect(serial string) error {
	_, err := runCmd(h.bin, "connect", serial)
	return err
}

func (h *AdbHelper) WaitForDevice(serial string) error {
	_, err := runCmd(h.bin, "-s", serial, "wait-for-device")
	return err
}

// Return combined stdout and stderr
func (h *AdbHelper) ExecShellCommand(serial string, cmd []string) (string, error) {
	if err := h.WaitForDevice(serial); err != nil {
		return "", fmt.Errorf("`wait-for-device` failed: %w", err)
	}
	return runCmd(h.bin, append([]string{"-s", serial, "shell"}, cmd...)...)
}

func (h *AdbHelper) BuildShellCommand(serial string, cmd []string) *exec.Cmd {
	return exec.Command(h.bin, append([]string{"-s", serial, "shell"}, cmd...)...)
}

func runCmd(name string, args ...string) (string, error) {
	cmd := exec.CommandContext(context.TODO(), name, args...)
	log.Printf("Executing command: `%s`\n", cmd.String())
	stdoutStderr, err := cmd.CombinedOutput()
	if len(stdoutStderr) > 0 {
		log.Printf("Combined stdout and stderr: %s\n", stdoutStderr)
	}
	if err != nil {
		return "", err
	}
	return string(stdoutStderr), nil
}

func CollectHOLogs(baseURL string) error {
	// `TEST_UNDECLARED_OUTPUTS_DIR` env var is defined by bazel
	// https://bazel.build/reference/test-encyclopedia#initial-conditions
	val, ok := os.LookupEnv("TEST_UNDECLARED_OUTPUTS_DIR")
	if !ok {
		return errors.New("`TEST_UNDECLARED_OUTPUTS_DIR` was not found")
	}
	filename := filepath.Join(val, "host_orchestrator.log")
	f, err := os.Create(filename)
	if err != nil {
		return err
	}
	client := hoclient.NewJournalGatewaydClient(baseURL)
	if err := client.PullHOLogs(f); err != nil {
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}
	return nil
}
