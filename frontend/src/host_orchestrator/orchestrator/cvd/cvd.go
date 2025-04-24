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
	CVDBin      = "/usr/bin/cvd"
	FetchCVDBin = "/usr/bin/fetch_cvd"
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

// Credentials to be passed to cvd fetch or cvd load.
// cvd supports multiple strategies to obtain these credentials, with
// different command line flags used for each of them.
type FetchCredentials interface {
	// Adds command line flags and other setup like inheriting file descriptors
	AddToCmd(cmd *exec.Cmd) error
}

// Build api credentials from a file (OAUTH2.0 access token).
// Optionally includes the project id associated with the client
type FetchTokenFileCredentials struct {
	AccessToken string
	ProjectId   string // optional
}

func (c *FetchTokenFileCredentials) AddToCmd(cmd *exec.Cmd) error {
	file, err := createCredentialsFile(c.AccessToken)
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
	return nil
}

// Build api credentials from the GCE instance's service account
type FetchGceCredentials struct{}

func (c *FetchGceCredentials) AddToCmd(cmd *exec.Cmd) error {
	// TODO(b/401592023) Use --use_gce_metadata when cvd load supports them
	cmd.Args = append(cmd.Args, "--credential_source=gce")
	return nil
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
	const begin = "############################################\n" +
		"## BEGIN \n" +
		"############################################"
	const end = "############################################\n" +
		"## END \n" +
		"############################################"
	log.Println(begin)
	defer log.Println(end)
	return cli.runCmd(cli.buildCmd(bin, args...))
}

func (cli *CLI) Load(configPath string, creds FetchCredentials) (*Group, error) {
	cmd := cli.buildCmd(CVDBin, "load", configPath)
	if creds != nil {
		creds.AddToCmd(cmd)
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
	HostPath     string
	ProductPath  string
	InstanceNums []uint32
}

func (o *CreateOptions) toArgs() []string {
	args := []string{}
	if o.HostPath != "" {
		args = append(args, "--host_path", o.HostPath)
	}
	if o.ProductPath != "" {
		args = append(args, "--product_path", o.ProductPath)
	}
	if len(o.InstanceNums) > 0 {
		args = append(args, "--instance_nums", strings.Join(sliceItoa(o.InstanceNums), ","))
		args = append(args, "--num_instances", fmt.Sprintf("%d", len(o.InstanceNums)))
	}
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

type FetchOpts struct {
	DefaultBuildID         string
	DefaultBuildTarget     string
	SystemImageBuildID     string
	SystemImageBuildTarget string
}

func (cli *CLI) Fetch(opts FetchOpts, creds FetchCredentials, targetDir string) error {
	if opts.DefaultBuildID == "" {
		return fmt.Errorf("default build id is required")
	}
	if opts.DefaultBuildTarget == "" {
		return fmt.Errorf("default build target is required")
	}
	args := []string{
		fmt.Sprintf("--directory=%s", targetDir),
		fmt.Sprintf("--default_build=%s/%s", opts.DefaultBuildID, opts.DefaultBuildTarget),
	}
	if opts.SystemImageBuildID != "" || opts.SystemImageBuildTarget != "" {
		if opts.SystemImageBuildID == "" || opts.SystemImageBuildTarget == "" {
			return fmt.Errorf(
				"either system image build and target are set or neither: build=%q, target=%q",
				opts.SystemImageBuildID, opts.SystemImageBuildTarget)
		}
		args = append(args, fmt.Sprintf("--system_build=%s/%s", opts.SystemImageBuildID, opts.SystemImageBuildTarget))
	}
	cmd := cli.buildCmd(FetchCVDBin, args...)
	if creds != nil {
		creds.AddToCmd(cmd)
	}
	if _, err := cli.runCmd(cmd); err != nil {
		return fmt.Errorf("`fetch_cvd` failed: %w", err)
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
