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
	"os"
	"os/exec"
	"strings"
)

type CVDExecContext = func(ctx context.Context, env []string, name string, arg ...string) *exec.Cmd

const (
	CVDBin      = "/usr/bin/cvd"
	FetchCVDBin = "/usr/bin/fetch_cvd"
)

const (
	envVarAndroidHostOut = "ANDROID_HOST_OUT"
)

type CommandOpts struct {
	AndroidHostOut string
	Home           string
	Stdout         io.Writer
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

func (c *Command) Run() error {
	cmd := c.execContext(context.TODO(), cvdEnv(c.opts.AndroidHostOut), c.cvdBin, c.args...)
	stderr := &bytes.Buffer{}
	cmd.Stdout = c.opts.Stdout
	cmd.Stderr = stderr
	if err := cmd.Start(); err != nil {
		return err
	}
	if err := cmd.Wait(); err != nil {
		LogStderr(cmd, stderr.String())
		return &CommandExecErr{c.args, stderr.String(), err}
	}
	return nil
}

func cvdEnv(androidHostOut string) []string {
	if androidHostOut == "" {
		return nil
	}
	// Make sure the current process' environment is inherited by cvd, some cvd subcommands, like
	// start, expect the PATH environment variable to be defined.
	return append(os.Environ(), envVarAndroidHostOut+"="+androidHostOut)
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

func Exec(ctx CVDExecContext, name string, args ...string) (string, error) {
	cmd := ctx(context.TODO(), nil, name, args...)
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	err := cmd.Run()
	if err != nil {
		return "", &CommandExecErr{args, stderr.String(), err}
	}
	return stdout.String(), nil
}
