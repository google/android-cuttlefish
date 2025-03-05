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
	"os/user"
	"path/filepath"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/artifacts"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/hashicorp/go-multierror"
)

type ExecContext = func(ctx context.Context, name string, arg ...string) *exec.Cmd

type Validator interface {
	Validate() error
}

type EmptyFieldError string

func (s EmptyFieldError) Error() string {
	return fmt.Sprintf("field %v is empty", string(s))
}

type AndroidBuild struct {
	ID     string
	Target string
}

type IMPaths struct {
	RootDir          string
	ArtifactsRootDir string
	CVDBugReportsDir string
	SnapshotsRootDir string
}

// Creates a CVD execution context from a regular execution context.
// If a non-nil user is provided the returned execution context executes commands as that user.
func newCVDExecContext(execContext ExecContext, usr *user.User) cvd.CVDExecContext {
	if usr != nil {
		return func(ctx context.Context, name string, arg ...string) *exec.Cmd {
			newArgs := []string{"-u", usr.Username}
			newArgs = append(newArgs, name)
			newArgs = append(newArgs, arg...)
			return execContext(ctx, "sudo", newArgs...)
		}
	}
	return func(ctx context.Context, name string, arg ...string) *exec.Cmd {
		cmd := execContext(ctx, name, arg...)
		return cmd
	}
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

func CVDLogsDir(ctx cvd.CVDExecContext, groupName, name string) (string, error) {
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

const (
	// TODO(b/267525748): Make these values configurable.
	mainBuildDefaultBranch = "aosp-main"
	mainBuildDefaultTarget = "aosp_cf_x86_64_phone-trunk_staging-userdebug"
)

func defaultMainBuild() *apiv1.AndroidCIBuild {
	return &apiv1.AndroidCIBuild{Branch: mainBuildDefaultBranch, Target: mainBuildDefaultTarget}
}

type fetchCVDCommandArtifactsFetcher struct {
	execContext         cvd.CVDExecContext
	buildAPICredentials BuildAPICredentials
}

func newFetchCVDCommandArtifactsFetcher(execContext cvd.CVDExecContext, buildAPICredentials BuildAPICredentials) *fetchCVDCommandArtifactsFetcher {
	return &fetchCVDCommandArtifactsFetcher{
		execContext:         execContext,
		buildAPICredentials: buildAPICredentials,
	}
}

// The artifacts directory gets created during the execution of `fetch_cvd` granting access to the cvdnetwork group
// which translated to granting the necessary permissions to the cvd executor user.
func (f *fetchCVDCommandArtifactsFetcher) Fetch(outDir, buildID, target string, extraOptions *artifacts.ExtraCVDOptions) error {
	cvdCLI := cvd.NewCLI(f.execContext)

	var creds cvd.FetchCredentials
	if f.buildAPICredentials.AccessToken != "" {
		creds = &cvd.FetchTokenFileCredentials{
			AccessToken: f.buildAPICredentials.AccessToken,
			ProjectId:   f.buildAPICredentials.UserProjectID,
		}
	} else if isRunningOnGCE() {
		if ok, err := hasServiceAccountAccessToken(); err != nil {
			log.Printf("service account token check failed: %s", err)
		} else if ok {
			creds = &cvd.FetchGceCredentials{}
		}
	}
	opts := cvd.FetchOpts{
		DefaultBuildID:     buildID,
		DefaultBuildTarget: target,
	}
	if extraOptions != nil {
		opts.SystemImageBuildID = extraOptions.SystemImgBuildID
		opts.SystemImageBuildTarget = extraOptions.SystemImgTarget
	}
	return cvdCLI.Fetch(opts, creds, outDir)
}

type buildAPIArtifactsFetcher struct {
	buildAPI artifacts.BuildAPI
}

func newBuildAPIArtifactsFetcher(buildAPI artifacts.BuildAPI) *buildAPIArtifactsFetcher {
	return &buildAPIArtifactsFetcher{
		buildAPI: buildAPI,
	}
}

func (f *buildAPIArtifactsFetcher) Fetch(outDir, buildID, target string, artifactNames ...string) error {
	var chans []chan error
	for _, name := range artifactNames {
		ch := make(chan error)
		chans = append(chans, ch)
		go func(name string) {
			defer close(ch)
			filename := outDir + "/" + name
			if err := downloadArtifactToFile(f.buildAPI, filename, name, buildID, target); err != nil {
				ch <- err
			}
		}(name)
	}
	var merr error
	for _, ch := range chans {
		for err := range ch {
			merr = multierror.Append(merr, err)
		}
	}
	return merr
}

const (
	// TODO(b/242599859): Add report_anonymous_usage_stats as a parameter to the Create CVD API.
	reportAnonymousUsageStats = true
	groupName                 = "cvd"
)

type startCVDParams struct {
	InstanceNumbers  []uint32
	MainArtifactsDir string
	RuntimeDir       string
	// OPTIONAL. If set, kernel relevant artifacts will be pulled from this dir.
	KernelDir string
	// OPTIONAL. If set, bootloader relevant artifacts will be pulled from this dir.
	BootloaderDir string
}

func CreateCVD(ctx cvd.CVDExecContext, p startCVDParams) (*cvd.Group, error) {
	createOpts := cvd.CreateOptions{
		HostPath:    p.MainArtifactsDir,
		ProductPath: p.MainArtifactsDir,
	}
	if len(p.InstanceNumbers) > 1 {
		createOpts.InstanceNums = p.InstanceNumbers
	}

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
	ExecContext ExecContext
}

func (v *HostValidator) Validate() error {
	if ok, _ := fileExist("/dev/kvm"); !ok {
		return operator.NewInternalError("Nested virtualization is not enabled.", nil)
	}
	return nil
}

// Helper to update the passed builds with latest green BuildID if build is not nil and BuildId is empty.
func updateBuildsWithLatestGreenBuildID(buildAPI artifacts.BuildAPI, builds []*apiv1.AndroidCIBuild) error {
	var chans []chan error
	for _, build := range builds {
		ch := make(chan error)
		chans = append(chans, ch)
		go func(build *apiv1.AndroidCIBuild) {
			defer close(ch)
			if build != nil && build.BuildID == "" {
				if err := updateBuildWithLatestGreenBuildID(buildAPI, build); err != nil {
					ch <- err
				}
			}
		}(build)
	}
	var merr error
	for _, ch := range chans {
		for err := range ch {
			merr = multierror.Append(merr, err)
		}
	}
	return merr
}

// Helper to update the passed `build` with latest green BuildID.
func updateBuildWithLatestGreenBuildID(buildAPI artifacts.BuildAPI, build *apiv1.AndroidCIBuild) error {
	buildID, err := buildAPI.GetLatestGreenBuildID(build.Branch, build.Target)
	if err != nil {
		return err
	}
	build.BuildID = buildID
	return nil
}

// Download artifacts helper. Fails if file already exists.
func downloadArtifactToFile(buildAPI artifacts.BuildAPI, filename, artifactName, buildID, target string) error {
	exist, err := fileExist(target)
	if err != nil {
		return fmt.Errorf("download artifact %q failed: %w", filename, err)
	}
	if exist {
		return fmt.Errorf("download artifact %q failed: file already exists", filename)
	}
	f, err := os.Create(filename)
	if err != nil {
		return fmt.Errorf("download artifact %q failed: %w", filename, err)
	}
	var downloadErr error
	defer func() {
		if err := f.Close(); err != nil {
			log.Printf("download artifact: failed closing %q, error: %v", filename, err)
		}
		if downloadErr != nil {
			if err := os.Remove(filename); err != nil {
				log.Printf("download artifact: failed removing %q: %v", filename, err)
			}
		}
	}()
	downloadErr = buildAPI.DownloadArtifact(artifactName, buildID, target, f)
	return downloadErr
}

func runAcloudSetup(execContext cvd.CVDExecContext, artifactsRootDir, artifactsDir string) {
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

func contains(s []uint32, e uint32) bool {
	for _, a := range s {
		if a == e {
			return true
		}
	}
	return false
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
