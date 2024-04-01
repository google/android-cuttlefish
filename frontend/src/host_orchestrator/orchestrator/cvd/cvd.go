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
	"fmt"
	"io"
	"log"
	"os/exec"
	"strings"
	"sync/atomic"
	"syscall"
	"time"
)

type CVDExecContext = func(ctx context.Context, env []string, name string, arg ...string) *exec.Cmd

const (
	CVDBin      = "/usr/bin/cvd"
	FetchCVDBin = "/usr/bin/fetch_cvd"

	CVDCommandDefaultTimeout = 30 * time.Second
)

const (
	envVarAndroidHostOut = "ANDROID_HOST_OUT"
)

type CommandOpts struct {
	AndroidHostOut string
	Home           string
	Stdout         io.Writer
	Timeout        time.Duration
}

type Command struct {
	execContext CVDExecContext
	cvdBin      string
	args        []string
	opts        CommandOpts
}

func NewCommand(execContext CVDExecContext, args []string, opts CommandOpts) *Command {
	return &Command{
		execContext: execContext,
		cvdBin:      CVDBin,
		args:        args,
		opts:        opts,
	}
}

type CommandExecErr struct {
	args   []string
	stderr string
	err    error
}

func (e *CommandExecErr) Error() string {
	return fmt.Sprintf("cvd execution with args %q failed with stderr:\n%s",
		strings.Join(e.args, " "),
		e.stderr)
}

func (e *CommandExecErr) Unwrap() error { return e.err }

type CommandTimeoutErr struct {
	args []string
}

func (e *CommandTimeoutErr) Error() string {
	return fmt.Sprintf("cvd execution with args %q timed out", strings.Join(e.args, " "))
}

func (c *Command) Run() error {
	// Makes sure cvd server daemon is running before executing the cvd command.
	if err := c.startCVDServer(); err != nil {
		return err
	}
	// TODO: Use `context.WithTimeout` if upgrading to go 1.19 as `exec.Cmd` adds the `Cancel` function field,
	// so the cancel logic could be customized to continue sending the SIGINT signal.
	cmd := c.execContext(context.TODO(), cvdEnv(c.opts.AndroidHostOut), c.cvdBin, c.args...)
	stderr := &bytes.Buffer{}
	cmd.Stdout = c.opts.Stdout
	cmd.Stderr = stderr
	if err := cmd.Start(); err != nil {
		return err
	}
	var timedOut atomic.Value
	timedOut.Store(false)
	timeout := CVDCommandDefaultTimeout
	if c.opts.Timeout != 0 {
		timeout = c.opts.Timeout
	}
	go func() {
		select {
		case <-time.After(timeout):
			// NOTE: Do not use SIGKILL to terminate cvd commands. cvd commands are run using
			// `sudo` and contrary to SIGINT, SIGKILL is not relayed to child processes.
			if err := cmd.Process.Signal(syscall.SIGINT); err != nil {
				log.Printf("error sending SIGINT signal %+v", err)
			}
			timedOut.Store(true)
		}
	}()
	if err := cmd.Wait(); err != nil {
		LogStderr(cmd, stderr.String())
		if timedOut.Load().(bool) {
			return &CommandTimeoutErr{c.args}
		}
		return &CommandExecErr{c.args, stderr.String(), err}
	}
	return nil
}

func (c *Command) startCVDServer() error {
	cmd := c.execContext(context.TODO(), cvdEnv(""), c.cvdBin)
	// NOTE: Stdout and Stderr should be nil so Run connects the corresponding
	// file descriptor to the null device (os.DevNull).
	// Otherwhise, `Run` will never complete. Why? a pipe will be created to handle
	// the data of the new process, this pipe will be passed over to `cvd_server`,
	// which is a daemon, hence the pipe will never reach EOF and Run will never
	// complete. Read more about it here: https://cs.opensource.google/go/go/+/refs/tags/go1.18.3:src/os/exec/exec.go;l=108-111
	cmd.Stdout = nil
	cmd.Stderr = nil
	return cmd.Run()
}

func cvdEnv(androidHostOut string) []string {
	env := []string{}
	if androidHostOut != "" {
		env = append(env, envVarAndroidHostOut+"="+androidHostOut)
	}
	return env
}

func OutputLogMessage(output string) string {
	const format = "############################################\n" +
		"## BEGIN \n" +
		"############################################\n" +
		"\n%s\n\n" +
		"############################################\n" +
		"## END \n" +
		"############################################\n"
	return fmt.Sprintf(format, string(output))
}

func LogStderr(cmd *exec.Cmd, val string) {
	msg := "`%s`, stderr:\n%s"
	log.Printf(msg, strings.Join(cmd.Args, " "), OutputLogMessage(val))
}

func LogCombinedStdoutStderr(cmd *exec.Cmd, val string) {
	msg := "`%s`, combined stdout and stderr :\n%s"
	log.Printf(msg, strings.Join(cmd.Args, " "), OutputLogMessage(val))
}

func Exec(ctx CVDExecContext, name string, args ...string) error {
	cmd := ctx(context.TODO(), nil, name, args...)
	var b bytes.Buffer
	cmd.Stdout = nil
	cmd.Stderr = &b
	err := cmd.Run()
	if err != nil {
		return &CommandExecErr{args, b.String(), err}
	}
	return nil
}
