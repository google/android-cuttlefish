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
	"context"
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
	ctx     context.Context
	keeper  *exec.Cmd
	pid     int
	tempdir string
	closed  bool
}

func NewSandbox(t *testing.T) *Sandbox {
	if err := checkPrereqs(); err != nil {
		t.Fatalf("sandbox prerequisites not met: %v", err)
	}

	ctx := t.Context()
	s := &Sandbox{ctx: ctx, tempdir: t.TempDir()}
	t.Cleanup(s.Close)

	// spawn the process that will keep the sandbox alive.
	// we use a PID namespace to ensure everything is torn down.
	cmd := exec.CommandContext(ctx, "unshare", "--user", "--map-root-user", "--net", "--mount", "--pid", "--fork", "--kill-child", "--mount-proc", "sleep", "infinity")
	if err := cmd.Start(); err != nil {
		t.Fatalf("cannot create rootless user+net namespace: %v", err)
	}
	s.keeper = cmd

	if err := s.waitReady(); err != nil {
		t.Fatalf("namespace not usable on this host: %v", err)
	}
	if err := s.setupFilesystem(); err != nil {
		t.Fatalf("failed to prepare filesystem sandbox: %v", err)
	}
	if err := s.setupNetwork(); err != nil {
		t.Fatalf("failed to prepare network sandbox: %v", err)
	}

	installDnsmasqShim(t, s)

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
		if s.pid == 0 {
			pid, err := childPid(s.keeper.Process.Pid)
			if err != nil {
				lastErr = err
				time.Sleep(50 * time.Millisecond)
				continue
			}
			s.pid = pid
		}
		c := exec.CommandContext(s.ctx, "nsenter", "-t", strconv.Itoa(s.pid), "-U", "-n", "-m", "-p", "--preserve-credentials", "--", "true")
		if err := c.Run(); err == nil {
			return nil
		} else {
			lastErr = err
		}
		time.Sleep(50 * time.Millisecond)
	}
	return fmt.Errorf("namespace did not become ready: %w", lastErr)
}

// we need to find the child PID since the keeper process stays in
// the host namespace.
func childPid(parent int) (int, error) {
	entries, err := os.ReadDir("/proc")
	if err != nil {
		return 0, fmt.Errorf("reading /proc: %w", err)
	}
	for _, e := range entries {
		pid, err := strconv.Atoi(e.Name())
		if err != nil {
			continue
		}
		status, err := os.ReadFile(fmt.Sprintf("/proc/%d/status", pid))
		if err != nil {
			continue
		}
		if ppidFromStatus(status) == parent {
			return pid, nil
		}
	}
	return 0, fmt.Errorf("no child of keeper pid %d found yet", parent)
}

func ppidFromStatus(status []byte) int {
	for _, line := range strings.Split(string(status), "\n") {
		if rest, ok := strings.CutPrefix(line, "PPid:"); ok {
			ppid, err := strconv.Atoi(strings.TrimSpace(rest))
			if err != nil {
				return -1
			}
			return ppid
		}
	}
	return -1
}

func (s *Sandbox) nsenterArgs(extra ...string) []string {
	base := []string{"nsenter", "-t", strconv.Itoa(s.pid), "-U", "-n", "-m", "-p", "--preserve-credentials", "--"}
	return append(base, extra...)
}

func (s *Sandbox) Run(args ...string) (CommandOutput, error) {
	full := s.nsenterArgs(args...)
	cmd := exec.CommandContext(s.ctx, full[0], full[1:]...)
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
