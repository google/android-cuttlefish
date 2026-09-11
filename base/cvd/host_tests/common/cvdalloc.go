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
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"syscall"
	"testing"
	"time"

	"github.com/bazelbuild/rules_go/go/runfiles"
)

const cvdallocRunDir = "/var/tmp/cvd"

const (
	cvdallocReadyTimeout    = 30 * time.Second
	cvdallocTeardownTimeout = 30 * time.Second
	cvdallocSignalTimeout   = 5 * time.Second
)

type Cvdalloc struct {
	sandbox *Sandbox
	bin     string
}

// NewCvdalloc resolves the cvdalloc binary under test and prepares the sandbox
// to run it.
//
// Resolution order:
//  1. $CVDALLOC_BIN, if set, is used verbatim (an explicit override).
//  2. Otherwise the binary is located through the Bazel runfiles of the
//     //cuttlefish/host/commands/cvdalloc:cvdalloc data dependency, whose
//     runfiles path is passed in $CVDALLOC_RLOCATION.
func NewCvdalloc(t *testing.T, s *Sandbox) *Cvdalloc {
	bin, err := resolveCvdallocBin()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(bin); err != nil {
		t.Fatalf("cvdalloc binary %q is not usable: %v", bin, err)
	}
	log.Printf("cvdalloc binary under test: %s", bin)

	// Prepare a tmpfs for the rundir.
	if _, err := s.Run("sh", "-c", fmt.Sprintf("mkdir -p %s && mount -t tmpfs tmpfs %s", cvdallocRunDir, cvdallocRunDir)); err != nil {
		t.Fatalf("failed to prepare cvdalloc run dir %q: %v", cvdallocRunDir, err)
	}

	return &Cvdalloc{sandbox: s, bin: bin}
}

func resolveCvdallocBin() (string, error) {
	if bin := os.Getenv("CVDALLOC_BIN"); bin != "" {
		fi, err := os.Stat(bin)
		if err != nil {
			return "", fmt.Errorf("CVDALLOC_BIN=%q is not usable: %v", bin, err)
		}
		if fi.IsDir() {
			return "", fmt.Errorf("CVDALLOC_BIN=%q is a directory, expected a binary", bin)
		}
		return bin, nil
	}

	rloc := os.Getenv("CVDALLOC_RLOCATION")
	if rloc == "" {
		return "", fmt.Errorf("CVDALLOC_RLOCATION is not set (expected the runfiles path " +
			"of //cuttlefish/host/commands/cvdalloc:cvdalloc); or set CVDALLOC_BIN to a binary")
	}
	bin, err := runfiles.Rlocation(rloc)
	if err != nil {
		return "", fmt.Errorf("failed to locate cvdalloc runfile %q: %v", rloc, err)
	}
	return bin, nil
}

func (c *Cvdalloc) Setup() error {
	_, err := c.sandbox.Run(c.bin, "--setup")
	return err
}

func (c *Cvdalloc) Teardown() error {
	_, err := c.sandbox.Run(c.bin, "--teardown")
	return err
}

type Instance struct {
	ID     int
	conn   net.Conn
	cmd    *exec.Cmd
	stdout *bytes.Buffer
	stderr *bytes.Buffer
}

// StartInstance launches "cvdalloc --id=<id> --socket=<fd>" inside the sandbox:
// 1. A socketpair is created
// 2. the peer end is passed to the child as fd 3 (inherited through nsenter)
// 3. this call blocks until the child signals that allocation is complete.
func (c *Cvdalloc) StartInstance(id int) (*Instance, error) {
	fds, err := syscall.Socketpair(syscall.AF_UNIX, syscall.SOCK_STREAM, 0)
	if err != nil {
		return nil, fmt.Errorf("socketpair: %w", err)
	}
	ourEnd := os.NewFile(uintptr(fds[0]), "cvdalloc-ctl")
	theirEnd := os.NewFile(uintptr(fds[1]), "cvdalloc-peer")

	// net.FileConn dups the fd and gives us deadline support; drop the original.
	conn, err := net.FileConn(ourEnd)
	ourEnd.Close()
	if err != nil {
		theirEnd.Close()
		return nil, fmt.Errorf("wrapping control socket: %w", err)
	}

	// ExtraFiles[0] becomes fd 3 in the child. nsenter preserves it across the
	// setns/exec into the sandbox, so cvdalloc sees the socket at --socket=3.
	args := c.sandbox.nsenterArgs(c.bin, fmt.Sprintf("--id=%d", id), "--socket=3")
	cmd := exec.CommandContext(c.sandbox.ctx, args[0], args[1:]...)
	cmd.ExtraFiles = []*os.File{theirEnd}
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	if err := cmd.Start(); err != nil {
		conn.Close()
		theirEnd.Close()
		return nil, fmt.Errorf("starting cvdalloc: %w", err)
	}
	// The child owns its own dup of the peer end now.
	theirEnd.Close()

	inst := &Instance{ID: id, conn: conn, cmd: cmd, stdout: &stdout, stderr: &stderr}

	// Wait for the child's readiness Post (a single byte).
	if err := recvByte(conn, cvdallocReadyTimeout); err != nil {
		inst.kill()
		return nil, fmt.Errorf("waiting for cvdalloc id=%d to finish allocation: %w%s", id, err, inst.stderrSuffix())
	}
	return inst, nil
}

// StopInstance signals the instance to tear down (a single byte), waits for its
// acknowledging Post, and then reaps the process.
func (c *Cvdalloc) StopInstance(inst *Instance) error {
	if err := sendByte(inst.conn, cvdallocSignalTimeout); err != nil {
		inst.kill()
		return fmt.Errorf("signaling teardown to cvdalloc id=%d: %w", inst.ID, err)
	}
	if err := recvByte(inst.conn, cvdallocTeardownTimeout); err != nil {
		inst.kill()
		return fmt.Errorf("waiting for cvdalloc id=%d teardown ack: %w%s", inst.ID, err, inst.stderrSuffix())
	}
	inst.conn.Close()
	if err := inst.cmd.Wait(); err != nil {
		return fmt.Errorf("cvdalloc id=%d exited with error: %w%s", inst.ID, err, inst.stderrSuffix())
	}
	return nil
}

func (i *Instance) kill() {
	i.conn.Close()
	if i.cmd.Process != nil {
		i.cmd.Process.Kill()
		i.cmd.Wait()
	}
}

func (i *Instance) stderrSuffix() string {
	s := strings.TrimSpace(i.stderr.String())
	if s == "" {
		return ""
	}
	return "\n--- cvdalloc stderr ---\n" + s
}

func recvByte(c net.Conn, timeout time.Duration) error {
	if err := c.SetReadDeadline(time.Now().Add(timeout)); err != nil {
		return err
	}
	buf := make([]byte, 1)
	if _, err := c.Read(buf); err != nil {
		return err
	}
	return nil
}

func sendByte(c net.Conn, timeout time.Duration) error {
	if err := c.SetWriteDeadline(time.Now().Add(timeout)); err != nil {
		return err
	}
	_, err := c.Write([]byte{0})
	return err
}

// CvdallocDnsmasqIfaces reports the interfaces for which cvdalloc started a
// dnsmasq, by inspecting the pidfiles it writes under CvdDir().
func CvdallocDnsmasqIfaces(s *Sandbox) []string {
	out, err := s.Run("sh", "-c", fmt.Sprintf("ls -1 %s/cuttlefish-dnsmasq-*.pid 2>/dev/null || true", cvdallocRunDir))
	if err != nil {
		return nil
	}
	var ifaces []string
	for _, line := range nonEmptyLines(out.Stdout) {
		base := filepath.Base(line)
		ifaces = append(ifaces, strings.TrimSuffix(strings.TrimPrefix(base, "cuttlefish-dnsmasq-"), ".pid"))
	}
	sort.Strings(ifaces)
	return ifaces
}
