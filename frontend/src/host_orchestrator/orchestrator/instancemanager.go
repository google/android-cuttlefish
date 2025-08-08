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

package orchestrator

import (
	"bytes"
	"context"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	hoexec "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type Validator interface {
	Validate() error
}

type EmptyFieldError string

func (s EmptyFieldError) Error() string {
	return fmt.Sprintf("field %v is empty", string(s))
}

type IMPaths struct {
	RootDir             string
	InstancesDir        string
	CVDBugReportsDir    string
	SnapshotsRootDir    string
	ImageDirectoriesDir string
}

func CvdGroupToAPIObject(group *cvd.Group) []*apiv1.CVD {
	result := make([]*apiv1.CVD, len(group.Instances))
	for i, item := range group.Instances {
		result[i] = CvdInstanceToAPIObject(item, group.Name)
	}
	return result
}

func CvdInstanceToAPIObject(instance *cvd.Instance, group string) *apiv1.CVD {
	return &apiv1.CVD{
		Group: group,
		Name:  instance.InstanceName,
		// TODO(b/259725479): Update when `cvd fleet` prints out build information.
		BuildSource:    &apiv1.BuildSource{},
		Status:         instance.Status,
		Displays:       instance.Displays,
		WebRTCDeviceID: instance.WebRTCDeviceID,
		ADBSerial:      instance.ADBSerial,
	}
}

func findGroup(fleet *cvd.Fleet, name string) (bool, *cvd.Group) {
	for _, e := range fleet.Groups {
		if e.Name == name {
			return true, e
		}
	}
	return false, nil
}

func findInstance(group *cvd.Group, name string) (bool, *cvd.Instance) {
	for _, e := range group.Instances {
		if e.InstanceName == name {
			return true, e
		}
	}
	return false, &cvd.Instance{}
}

func CVDLogsDir(ctx hoexec.ExecContext, groupName, name string) (string, error) {
	cvdCLI := cvd.NewCLI(ctx)
	fleet, err := cvdCLI.Fleet()
	if err != nil {
		return "", err
	}
	ok, group := findGroup(fleet, groupName)
	if !ok {
		return "", operator.NewNotFoundError(fmt.Sprintf("Group %q not found", groupName), nil)
	}
	ok, ins := findInstance(group, name)
	if !ok {
		return "", operator.NewNotFoundError(fmt.Sprintf("Instance %q not found", name), nil)
	}
	return ins.InstanceDir + "/logs", nil
}

type fetchCVDCommandArtifactsFetcher struct {
	execContext      hoexec.ExecContext
	fetchCredentials cvd.FetchCredentials
	buildAPIBaseURL  string
}

type ExtraCVDOptions struct {
	KernelBuild      cvd.AndroidBuild
	BootloaderBuild  cvd.AndroidBuild
	SystemImageBuild cvd.AndroidBuild
}

type CVDBundleFetcher interface {
	// Fetches artifacts to launch a Cuttlefish device
	Fetch(outDir string, mainBuild cvd.AndroidBuild, opts ExtraCVDOptions) error
}

func newFetchCVDCommandArtifactsFetcher(
	execContext hoexec.ExecContext, fetchCredentials cvd.FetchCredentials, buildAPIBaseURL string) *fetchCVDCommandArtifactsFetcher {
	return &fetchCVDCommandArtifactsFetcher{
		execContext:      execContext,
		fetchCredentials: fetchCredentials,
		buildAPIBaseURL:  buildAPIBaseURL,
	}
}

// The artifacts directory gets created during the execution of `fetch_cvd` granting access to the cvdnetwork group
// which translated to granting the necessary permissions to the cvd executor user.
func (f *fetchCVDCommandArtifactsFetcher) Fetch(outDir string, mainBuild cvd.AndroidBuild, opts ExtraCVDOptions) error {
	fetchOpts := cvd.FetchOpts{
		BuildAPIBaseURL:  f.buildAPIBaseURL,
		Credentials:      f.fetchCredentials,
		KernelBuild:      opts.KernelBuild,
		BootloaderBuild:  opts.BootloaderBuild,
		SystemImageBuild: opts.SystemImageBuild,
	}
	cvdCLI := cvd.NewCLI(f.execContext)
	return cvdCLI.Fetch(mainBuild, outDir, fetchOpts)
}

const (
	// TODO(b/242599859): Add report_anonymous_usage_stats as a parameter to the Create CVD API.
	reportAnonymousUsageStats = true
	groupName                 = "cvd"
)

type startCVDParams struct {
	InstanceCount    uint32
	MainArtifactsDir string
	// OPTIONAL. If set, kernel relevant artifacts will be pulled from this dir.
	KernelDir string
	// OPTIONAL. If set, bootloader relevant artifacts will be pulled from this dir.
	BootloaderDir string
}

func CreateCVD(ctx hoexec.ExecContext, p startCVDParams) (*cvd.Group, error) {
	createOpts := cvd.CreateOptions{
		HostPath:    p.MainArtifactsDir,
		ProductPath: p.MainArtifactsDir,
	}
	createOpts.InstanceCount = p.InstanceCount

	startOpts := cvd.StartOptions{
		ReportUsageStats: reportAnonymousUsageStats,
	}
	if p.KernelDir != "" {
		startOpts.KernelImage = fmt.Sprintf("%s/bzImage", p.KernelDir)
		initramfs := filepath.Join(p.KernelDir, "initramfs.img")
		if exist, _ := fileExist(initramfs); exist {
			startOpts.InitramfsImage = initramfs
		}
	}
	if p.BootloaderDir != "" {
		startOpts.BootloaderRom = fmt.Sprintf("%s/u-boot.rom", p.BootloaderDir)
	}

	cvdCLI := cvd.NewCLI(ctx)
	group, err := cvdCLI.Create(cvd.Selector{Group: groupName}, createOpts, startOpts)
	if err != nil {
		return nil, fmt.Errorf("launch cvd stage failed: %w", err)
	}
	return group, nil
}

// Fails if the directory already exists.
func createNewDir(dir string) error {
	err := os.Mkdir(dir, 0774)
	if err != nil {
		return err
	}
	// Sets dir permission regardless of umask.
	return os.Chmod(dir, 0774)
}

func createDir(dir string) error {
	if err := createNewDir(dir); os.IsExist(err) {
		return nil
	} else {
		return err
	}
}

func fileExist(name string) (bool, error) {
	if _, err := os.Stat(name); err == nil {
		return true, nil
	} else if os.IsNotExist(err) {
		return false, nil
	} else {
		return false, err
	}
}

// Validates whether the current host is valid to run CVDs.
type HostValidator struct {
	ExecContext hoexec.ExecContext
}

func (v *HostValidator) Validate() error {
	if ok, _ := fileExist("/dev/kvm"); !ok {
		return operator.NewInternalError("Nested virtualization is not enabled.", nil)
	}
	return nil
}

func runAcloudSetup(execContext hoexec.ExecContext, artifactsRootDir, artifactsDir string) {
	run := func(cmd *exec.Cmd) {
		var b bytes.Buffer
		cmd.Stdout = &b
		cmd.Stderr = &b
		err := cmd.Run()
		if err != nil {
			log.Println("runAcloudSetup failed with error: " + b.String())
		}
	}
	// Creates symbolic link `acloud_link` which points to the passed device artifacts directory.
	go run(execContext(context.TODO(), "ln", "-s", artifactsDir, artifactsRootDir+"/acloud_link"))
}

func isRunningOnGCE() bool {
	_, err := net.LookupIP("metadata.google.internal")
	return err == nil
}

// For instances running on GCE, checks whether the instance was created with a service account having an access token.
func hasServiceAccountAccessToken() (bool, error) {
	req, err := http.NewRequest("GET", "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token", nil)
	if err != nil {
		return false, err
	}
	req.Header.Set("Metadata-Flavor", "Google")
	client := &http.Client{}
	res, err := client.Do(req)
	if err != nil {
		return false, err
	}
	return res.StatusCode == http.StatusOK, nil
}
