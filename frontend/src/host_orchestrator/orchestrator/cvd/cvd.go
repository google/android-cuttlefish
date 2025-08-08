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

package cvd

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"

	hoexec "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
)

const (
	CVDBin = "/usr/bin/cvd"
)

// A CVD instance group
type Group struct {
	Name      string      `json:"group_name"`
	Instances []*Instance `json:"instances"`
}

// A CVD instance
type Instance struct {
	InstanceName   string   `json:"instance_name"`
	Status         string   `json:"status"`
	Displays       []string `json:"displays"`
	InstanceDir    string   `json:"instance_dir"`
	WebRTCDeviceID string   `json:"webrtc_device_id"`
	ADBSerial      string   `json:"adb_serial"`
}

// The output of the cvd fleet command
type Fleet struct {
	Groups []*Group `json:"groups"`
}

type FetchCredentials struct {
	// If on GCE, indicates whether to use the credentials of the
	// service account running the the machine.
	UseGCEServiceAccountCredentials bool
	// OAUTH2.0 Access token.
	AccessTokenCredentials AccessTokenCredentials
}

type AccessTokenCredentials struct {
	AccessToken string
	// The credential for exchanging access tokens should be generated from a GCP project that
	// has the Build API enabled. If it isn't, UserProjectID is required for successful API usage.
	// The value of UserProjectID is expected to be the project ID of a GCP project that has the
	// Build API enabled. This project ID can differ from the one used to generate OAuth credentials.
	UserProjectID string // optional
}

// A filter allowing to select instances or groups by name.
type Selector struct {
	Group    string
	Instance string
}

func (s *Selector) asArgs() []string {
	res := []string{}
	if s.Group != "" {
		res = append(res, "--group_name="+s.Group)
	}
	if s.Instance != "" {
		res = append(res, "--instance_name="+s.Instance)
	}
	return res
}

// Wrapper around the cvd command, exposing the tool's subcommands as functions
type CLI struct {
	execContext hoexec.ExecContext
}

func NewCLI(execCtx hoexec.ExecContext) *CLI {
	return &CLI{execCtx}
}

func (cli *CLI) buildCmd(bin string, args ...string) *exec.Cmd {
	return cli.execContext(context.TODO(), bin, args...)
}

func (cli *CLI) runCmd(cmd *exec.Cmd) ([]byte, error) {
	log.Printf("runCmd: %s", cmd.String())
	stdoutBuff := &bytes.Buffer{}
	stdoutMw := io.MultiWriter(stdoutBuff, log.Writer())
	cmd.Stdout = stdoutMw
	stderrBuff := &bytes.Buffer{}
	stderrMw := io.MultiWriter(stderrBuff, log.Writer())
	cmd.Stderr = stderrMw
	if err := cmd.Start(); err != nil {
		return stdoutBuff.Bytes(), err
	}
	if err := cmd.Wait(); err != nil {
		return stdoutBuff.Bytes(),
			fmt.Errorf(
				"execution of %q command with args %q failed: %w\n Stderr: \n%s",
				cmd.Path, cmd.Args, err, stderrBuff.String())
	}
	return stdoutBuff.Bytes(), nil
}

// Runs the given command returning the stdout output as a byte array.
// Both stdout and stderr output also written to the logs.
func (cli *CLI) exec(bin string, args ...string) ([]byte, error) {
	return cli.runCmd(cli.buildCmd(bin, args...))
}

type LoadOpts struct {
	BuildAPIBaseURL string
	Credentials     FetchCredentials
}

func (cli *CLI) Load(configPath string, opts LoadOpts) (*Group, error) {
	args := []string{"load", configPath}
	if opts.BuildAPIBaseURL != "" {
		args = append(args, fmt.Sprintf("--override=fetch.api_base_url:%s", opts.BuildAPIBaseURL))
	}

	cmd := cli.buildCmd(CVDBin, args...)

	if opts.Credentials.UseGCEServiceAccountCredentials {
		cmd.Args = append(cmd.Args, "--credential_source=gce")
	} else if opts.Credentials.AccessTokenCredentials != (AccessTokenCredentials{}) {
		file, err := createCredentialsFile(opts.Credentials.AccessTokenCredentials.AccessToken)
		if err != nil {
			return nil, err
		}
		defer file.Close()
		// This is necessary for the subprocess to inherit the file.
		cmd.ExtraFiles = append(cmd.ExtraFiles, file)
		// The actual fd number is not retained, the lowest available number is used instead.
		fd := 3 + len(cmd.ExtraFiles) - 1
		// TODO(b/401592023) Use --credential_filepath when cvd load supports it
		cmd.Args = append(cmd.Args, fmt.Sprintf("--credential_source=/proc/self/fd/%d", fd))
	}

	out, err := cli.runCmd(cmd)
	if err != nil {
		return nil, fmt.Errorf("failed execution of `cvd load`: %w", err)
	}
	var group Group
	if err := json.Unmarshal(out, &group); err != nil {
		return nil, fmt.Errorf("failed parsing `cvd load` output: %w", err)
	}
	return &group, nil
}

type CreateOptions struct {
	HostPath      string
	ProductPath   string
	InstanceCount uint32
}

func (o *CreateOptions) toArgs() []string {
	args := []string{}
	if o.HostPath != "" {
		args = append(args, "--host_path", o.HostPath)
	}
	if o.ProductPath != "" {
		args = append(args, "--product_path", o.ProductPath)
	}
	args = append(args, "--num_instances", fmt.Sprintf("%d", o.InstanceCount))
	return args
}

func (cli *CLI) Create(selector Selector, createOpts CreateOptions, startOpts StartOptions) (*Group, error) {
	args := selector.asArgs()
	args = append(args, "create")
	args = append(args, createOpts.toArgs()...)
	args = append(args, startOpts.toArgs()...)
	out, err := cli.exec(CVDBin, args...)
	if err != nil {
		return nil, fmt.Errorf("failed execution of `cvd %s`: %w", strings.Join(args, " "), err)
	}
	var group Group
	if err := json.Unmarshal(out, &group); err != nil {
		return nil, fmt.Errorf("failed parsing `cvd create` output: %w", err)
	}
	return &group, nil
}

type StartOptions struct {
	SnapshotPath     string
	KernelImage      string
	InitramfsImage   string
	BootloaderRom    string
	ReportUsageStats bool
}

func (o *StartOptions) toArgs() []string {
	args := []string{}
	if o.SnapshotPath != "" {
		args = append(args, "--snapshot_path", o.SnapshotPath)
	}
	if o.KernelImage != "" {
		args = append(args, "--kernel_path", o.KernelImage)
	}
	if o.InitramfsImage != "" {
		args = append(args, "--initramfs_path", o.InitramfsImage)
	}
	if o.BootloaderRom != "" {
		args = append(args, "--bootloader", o.BootloaderRom)
	}
	if o.ReportUsageStats {
		args = append(args, "--report_anonymous_usage_stats=y")
	} else {
		args = append(args, "--report_anonymous_usage_stats=n")
	}
	return args
}

func (cli *CLI) Start(selector Selector, opts StartOptions) error {
	args := selector.asArgs()
	args = append(args, "start", "--report_anonymous_usage_stats=y")
	args = append(args, opts.toArgs()...)
	_, err := cli.exec(CVDBin, args...)
	return err
}

func (cli *CLI) Stop(selector Selector) error {
	args := selector.asArgs()
	args = append(args, "stop")
	_, err := cli.exec(CVDBin, args...)
	return err
}

func (cli *CLI) Remove(selector Selector) error {
	args := selector.asArgs()
	args = append(args, "remove")
	_, err := cli.exec(CVDBin, args...)
	return err
}

func (cli *CLI) Fleet() (*Fleet, error) {
	out, err := cli.exec(CVDBin, "fleet")
	if err != nil {
		return nil, fmt.Errorf("failed execution of `cvd fleet`: %w", err)
	}
	fleet := &Fleet{}
	if err := json.Unmarshal(out, fleet); err != nil {
		return nil, fmt.Errorf("error parsing `cvd fleet` output: %w", err)
	}
	return fleet, nil
}

func (cli *CLI) BugReport(selector Selector, includeADBBugReport bool, dst string) error {
	args := selector.asArgs()
	args = append(args, []string{"host_bugreport", "--output=" + dst}...)
	if includeADBBugReport {
		args = append(args, []string{"--include_adb_bugreport=true"}...)
	}
	_, err := cli.exec(CVDBin, args...)
	return err
}

func (cli *CLI) Suspend(selector Selector) error {
	args := selector.asArgs()
	args = append(args, "suspend")
	_, err := cli.exec(CVDBin, args...)
	return err
}

func (cli *CLI) Resume(selector Selector) error {
	args := selector.asArgs()
	args = append(args, "resume")
	_, err := cli.exec(CVDBin, args...)
	return err
}

func (cli *CLI) TakeSnapshot(selector Selector, dir string) error {
	args := selector.asArgs()
	args = append(args, "snapshot_take", "--snapshot_path", dir)
	_, err := cli.exec(CVDBin, args...)
	return err
}

type DisplayAddOpts struct {
	Width         int
	Height        int
	DPI           int
	RefreshRateHZ int
}

func (o *DisplayAddOpts) toArg() string {
	return fmt.Sprintf("--display=width=%d,height=%d,dpi=%d,refresh_rate_hz=%d", o.Width, o.Height, o.DPI, o.RefreshRateHZ)
}

func (cli *CLI) DisplayAdd(selector Selector, opts DisplayAddOpts) error {
	args := selector.asArgs()
	args = append(args, "display")
	args = append(args, "add")
	args = append(args, opts.toArg())
	_, err := cli.exec(CVDBin, args...)
	return err
}

type DisplayMode struct {
	Windowed []int `json:"windowed"`
}

type Display struct {
	DPI           []int       `json:"dpi"`
	Mode          DisplayMode `json:"mode"`
	RefreshRateHZ int         `json:"refresh-rate"`
}

type Displays struct {
	Displays map[int]*Display `json:"displays"`
}

func (cli *CLI) DisplayList(selector Selector) (*Displays, error) {
	args := selector.asArgs()
	args = append(args, "display")
	args = append(args, "list")

	out, err := cli.exec(CVDBin, args...)
	if err != nil {
		return nil, fmt.Errorf("failed execution of `cvd display list`: %w", err)
	}
	displays := &Displays{}
	if err := json.Unmarshal(out, displays); err != nil {
		return nil, fmt.Errorf("error parsing `cvd display list` output: %w", err)
	}
	return displays, nil
}

func (cli *CLI) DisplayRemove(selector Selector, displayNumber int) error {
	args := selector.asArgs()
	args = append(args, "display")
	args = append(args, "remove")
	args = append(args, fmt.Sprintf("--display=%d", displayNumber))
	_, err := cli.exec(CVDBin, args...)
	return err
}

func (cli *CLI) DisplayScreenshot(selector Selector, displayNumber int, path string) error {
	args := selector.asArgs()
	args = append(args, "display")
	args = append(args, "screenshot")
	args = append(args, fmt.Sprintf("--display_number=%d", displayNumber))
	args = append(args, "--screenshot_path="+path)
	_, err := cli.exec(CVDBin, args...)
	return err
}

func (cli *CLI) PowerWash(selector Selector) error {
	args := selector.asArgs()
	args = append(args, "powerwash")
	_, err := cli.exec(CVDBin, args...)
	return err
}

func (cli *CLI) PowerBtn(selector Selector) error {
	args := selector.asArgs()
	args = append(args, "powerbtn")
	_, err := cli.exec(CVDBin, args...)
	return err
}

type AndroidBuild struct {
	BuildID string
	// Discarded if BuildID is set.
	// When `Branch` field is set, the latest green build id will be used.
	Branch      string
	BuildTarget string
}

func (b *AndroidBuild) Validate() error {
	if b.BuildID == "" && b.Branch == "" {
		return errors.New("build id and branch are empty, use one of them")
	}
	if b.BuildTarget == "" {
		return errors.New("build target is empty")
	}
	return nil
}

type FetchOpts struct {
	BuildAPIBaseURL  string
	Credentials      FetchCredentials
	KernelBuild      AndroidBuild
	BootloaderBuild  AndroidBuild
	SystemImageBuild AndroidBuild
}

func (cli *CLI) Fetch(mainBuild AndroidBuild, targetDir string, opts FetchOpts) error {
	if err := mainBuild.Validate(); err != nil {
		return fmt.Errorf("invalid main build: %w", err)
	}
	args := []string{
		"fetch",
		fmt.Sprintf("--directory=%s", targetDir),
		fmt.Sprintf("--default_build=%s", cliFormat(mainBuild)),
	}
	if opts.SystemImageBuild != (AndroidBuild{}) {
		build := opts.SystemImageBuild
		if err := build.Validate(); err != nil {
			return fmt.Errorf("invalid system image build: %w", err)
		}
		args = append(args, fmt.Sprintf("--system_build=%s", cliFormat(build)))
	}
	if opts.KernelBuild != (AndroidBuild{}) {
		build := opts.KernelBuild
		if err := build.Validate(); err != nil {
			return fmt.Errorf("invalid kernel build: %w", err)
		}
		args = append(args, fmt.Sprintf("--kernel_build=%s", cliFormat(build)))
	}
	if opts.BootloaderBuild != (AndroidBuild{}) {
		build := opts.BootloaderBuild
		if err := build.Validate(); err != nil {
			return fmt.Errorf("invalid bootloader build: %w", err)
		}
		args = append(args, fmt.Sprintf("--bootloader_build=%s", cliFormat(build)))
	}
	if opts.BuildAPIBaseURL != "" {
		args = append(args, fmt.Sprintf("--api_base_url=%s", opts.BuildAPIBaseURL))
	}

	cmd := cli.buildCmd(CVDBin, args...)

	if opts.Credentials.UseGCEServiceAccountCredentials {
		cmd.Args = append(cmd.Args, "--credential_source=gce")
	} else if opts.Credentials.AccessTokenCredentials != (AccessTokenCredentials{}) {
		file, err := createCredentialsFile(opts.Credentials.AccessTokenCredentials.AccessToken)
		if err != nil {
			return err
		}
		defer file.Close()
		// This is necessary for the subprocess to inherit the file.
		cmd.ExtraFiles = append(cmd.ExtraFiles, file)
		// The actual fd number is not retained, the lowest available number is used instead.
		fd := 3 + len(cmd.ExtraFiles) - 1
		// TODO(b/401592023) Use --credential_filepath when cvd load supports it
		cmd.Args = append(cmd.Args, fmt.Sprintf("--credential_source=/proc/self/fd/%d", fd))
	}

	if _, err := cli.runCmd(cmd); err != nil {
		return fmt.Errorf("`cvd fetch` failed: %w", err)
	}
	// TODO(b/286466643): Remove this hack once cuttlefish is capable of booting from read-only artifacts again.
	if _, err := cli.exec("chmod", "-R", "g+rw", targetDir); err != nil {
		return err
	}
	return nil
}

func sliceItoa(s []uint32) []string {
	result := make([]string, len(s))
	for i, v := range s {
		result[i] = strconv.Itoa(int(v))
	}
	return result
}

func createCredentialsFile(content string) (*os.File, error) {
	p1, p2, err := os.Pipe()
	if err != nil {
		return nil, fmt.Errorf("failed to create pipe for credentials: %w", err)
	}
	go func(f *os.File) {
		defer f.Close()
		if _, err := f.Write([]byte(content)); err != nil {
			log.Printf("Failed to write credentials to file: %v\n", err)
			// Can't return this error without risking a deadlock when the pipe buffer fills up.
		}
	}(p2)
	return p1, nil
}

func cliFormat(b AndroidBuild) string {
	build := b.BuildID
	if build == "" {
		build = b.Branch
	}
	return fmt.Sprintf("%s/%s", build, b.BuildTarget)
}
