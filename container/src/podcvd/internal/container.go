// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package internal

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/client"
	"github.com/docker/docker/pkg/stdcopy"
)

type CuttlefishContainerManager interface {
	GetClient() *client.Client
	// Check whether an image with the given name exists on the container engine or not
	ImageExists(ctx context.Context, name string) (bool, error)
	// Pull the container image
	PullImage(ctx context.Context, name string) error
	// Create and start a container instance with raw extra flags
	CreateAndStartContainer(ctx context.Context, extraFlags []string, name string) (string, error)
	// Execute a command on a running container instance
	ExecOnContainer(ctx context.Context, ctr string, cmd []string, stdin io.Reader, stdout io.Writer, stderr io.Writer) error
	// Stop and remove a container instance
	StopAndRemoveContainer(ctx context.Context, ctr string) error
}

type CuttlefishContainerManagerImpl struct {
	cli *client.Client
}

func NewCuttlefishContainerManager() (CuttlefishContainerManager, error) {
	cliopts := []client.Opt{
		client.WithAPIVersionNegotiation(),
		client.WithHost(RootlessPodmanSocketAddr()),
	}
	cli, err := client.NewClientWithOpts(cliopts...)
	if err != nil {
		return nil, fmt.Errorf("failed to create a new container engine client: %w", err)
	}
	return &CuttlefishContainerManagerImpl{
		cli: cli,
	}, nil
}

func (m *CuttlefishContainerManagerImpl) GetClient() *client.Client {
	return m.cli
}

func (m *CuttlefishContainerManagerImpl) ImageExists(ctx context.Context, name string) (bool, error) {
	cmd := exec.CommandContext(ctx, "podman", "image", "exists", name)
	err := cmd.Run()
	if err == nil {
		return true, nil
	}
	if _, ok := err.(*exec.ExitError); ok {
		return false, nil
	}
	return false, fmt.Errorf("failed to check container image existence: %w", err)
}

func (m *CuttlefishContainerManagerImpl) PullImage(ctx context.Context, name string) error {
	cmd := exec.CommandContext(ctx, "podman", "pull", name)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to pull container image %q: %w", name, err)
	}
	return nil
}

func (m *CuttlefishContainerManagerImpl) CreateAndStartContainer(ctx context.Context, extraFlags []string, name string) (string, error) {
	args := []string{"run", "-d", "-t", "--cap-add", "NET_ADMIN"}
	devices := []string{
		"/dev/kvm",
		"/dev/net/tun",
		"/dev/vhost-net",
		"/dev/vhost-vsock",
	}
	for _, dev := range devices {
		args = append(args, "--device", dev+":"+dev+":rwm")
	}
	args = append(args, extraFlags...)
	if name != "" {
		args = append(args, "--name", name)
	}
	args = append(args, imageName)
	cmd := exec.CommandContext(ctx, "podman", args...)
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		log.Printf("podman run failed: %s", stderr.String())
		return "", fmt.Errorf("failed to create and start container: %w", err)
	}
	return strings.TrimSpace(stdout.String()), nil
}

func (m *CuttlefishContainerManagerImpl) ExecOnContainer(ctx context.Context, ctr string, cmd []string, stdin io.Reader, stdout io.Writer, stderr io.Writer) error {
	execConfig := container.ExecOptions{
		AttachStderr: stderr != nil,
		AttachStdin:  stdin != nil,
		AttachStdout: stdout != nil,
		Cmd:          cmd,
		Tty:          false,
	}
	createRes, err := m.cli.ContainerExecCreate(ctx, ctr, execConfig)
	if err != nil {
		return fmt.Errorf("failed to create container execution %q: %w", strings.Join(cmd, " "), err)
	}
	attachRes, err := m.cli.ContainerExecAttach(ctx, createRes.ID, container.ExecStartOptions{})
	if err != nil {
		return fmt.Errorf("failed to attach container execution %q: %w", strings.Join(cmd, " "), err)
	}
	defer attachRes.Close()
	if stdin != nil {
		go func() {
			io.Copy(attachRes.Conn, stdin)
			attachRes.CloseWrite()
		}()
	}
	stdcopy.StdCopy(stdout, stderr, attachRes.Reader)

	if result, err := m.cli.ContainerExecInspect(ctx, createRes.ID); err != nil {
		return fmt.Errorf("failed to run command on the container: %w", err)
	} else if result.ExitCode != 0 {
		return fmt.Errorf("failed to run command on the container with exit code %d", result.ExitCode)
	}
	return nil
}

func (m *CuttlefishContainerManagerImpl) StopAndRemoveContainer(ctx context.Context, ctr string) error {
	timeout := int(0)
	stopConfig := container.StopOptions{
		Signal:  "SIGKILL",
		Timeout: &timeout,
	}
	errs := []error{}
	if err := m.cli.ContainerStop(ctx, ctr, stopConfig); err != nil {
		errs = append(errs, fmt.Errorf("failed to stop docker container: %w", err))
	}
	if err := m.cli.ContainerRemove(ctx, ctr, container.RemoveOptions{}); err != nil {
		errs = append(errs, fmt.Errorf("failed to remove docker container: %w", err))
	}
	return errors.Join(errs...)
}

func RootlessPodmanSocketAddr() string {
	socketPath := filepath.Join(os.Getenv("XDG_RUNTIME_DIR"), "podman/podman.sock")
	return fmt.Sprintf("unix://%s", socketPath)
}
