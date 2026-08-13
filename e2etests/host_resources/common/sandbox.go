// Copyright (C) 2026 The Android Open Source Project
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

package common

import (
	"bytes"
	"errors"
	"fmt"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"testing"
	"time"
)

type CommandOutput struct {
	Stdout string
	Stderr string
}

type Sandbox struct {
	t       *testing.T
	keeper  *exec.Cmd
	pid     int
	tempdir string
	closed  bool
}

func NewSandbox(t *testing.T) *Sandbox {
	if err := checkPrereqs(); err != nil {
		t.Fatalf("sandbox prerequisites not met: %v", err)
	}

	s := &Sandbox{t: t, tempdir: t.TempDir()}
	t.Cleanup(s.Close)

	// spawn the process that will keep the sandbox alive
	cmd := exec.Command("unshare", "--user", "--map-root-user", "--net", "--mount", "sleep", "infinity")
	if err := cmd.Start(); err != nil {
		t.Fatalf("cannot create rootless user+net namespace: %v", err)
	}
	s.keeper = cmd
	s.pid = cmd.Process.Pid

	if err := s.waitReady(); err != nil {
		t.Fatalf("namespace not usable on this host: %v", err)
	}
	if err := s.setupFilesystem(); err != nil {
		t.Fatalf("failed to prepare filesystem sandbox: %v", err)
	}
	if err := s.setupNetwork(); err != nil {
		t.Fatalf("failed to prepare network sandbox: %v", err)
	}

	return s
}

func checkPrereqs() error {
	if err := checkNamespacePrereqs(); err != nil {
		return err
	}
	return checkNetworkPrereqs()
}

func checkNamespacePrereqs() error {
	if b, err := os.ReadFile("/proc/sys/kernel/unprivileged_userns_clone"); err == nil {
		if strings.TrimSpace(string(b)) == "0" {
			return errors.New("unprivileged user namespaces are disabled (unprivileged_userns_clone=0)")
		}
	}
	for _, bin := range []string{"unshare", "nsenter", "sleep"} {
		if _, err := exec.LookPath(bin); err != nil {
			return fmt.Errorf("required binary %q not found on PATH: %w", bin, err)
		}
	}
	return nil
}

func (s *Sandbox) waitReady() error {
	deadline := time.Now().Add(5 * time.Second)
	var lastErr error
	for time.Now().Before(deadline) {
		c := exec.Command("nsenter", "-t", strconv.Itoa(s.pid), "-U", "-n", "-m", "--preserve-credentials", "--", "true")
		if err := c.Run(); err == nil {
			return nil
		} else {
			lastErr = err
		}
		time.Sleep(50 * time.Millisecond)
	}
	return fmt.Errorf("namespace did not become ready: %w", lastErr)
}

func (s *Sandbox) nsenterArgs(extra ...string) []string {
	base := []string{"nsenter", "-t", strconv.Itoa(s.pid), "-U", "-n", "-m", "--preserve-credentials", "--"}
	return append(base, extra...)
}

func (s *Sandbox) Run(args ...string) (CommandOutput, error) {
	full := s.nsenterArgs(args...)
	cmd := exec.Command(full[0], full[1:]...)
	var outBuf, errBuf bytes.Buffer
	cmd.Stdout = &outBuf
	cmd.Stderr = &errBuf
	log.Printf("[sandbox] running: %s", strings.Join(args, " "))
	err := cmd.Run()
	out := CommandOutput{Stdout: outBuf.String(), Stderr: errBuf.String()}
	if err != nil {
		return out, fmt.Errorf("command %q failed: %w (stderr: %s)", strings.Join(args, " "), err, strings.TrimSpace(errBuf.String()))
	}
	return out, nil
}

func (s *Sandbox) Close() {
	if s.closed {
		return
	}
	s.closed = true
	if s.keeper != nil && s.keeper.Process != nil {
		s.keeper.Process.Kill()
		s.keeper.Wait()
	}
}
