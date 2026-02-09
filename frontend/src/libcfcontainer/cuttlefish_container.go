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

package libcfcontainer

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"

	"dario.cat/mergo"
	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/api/types/image"
	"github.com/docker/docker/client"
	"github.com/docker/docker/pkg/stdcopy"
)

type CuttlefishContainerManager interface {
	GetClient() *client.Client
	// Check whether an image with the given name exists on the container engine or not
	ImageExists(ctx context.Context, name string) (bool, error)
	// Pull the container image
	PullImage(ctx context.Context, name string) error
	// Create and start a container instance
	CreateAndStartContainer(ctx context.Context, additionalConfig *container.Config, additionalHostConfig *container.HostConfig, name string) (string, error)
	// Execute a command on a running container instance
	ExecOnContainer(ctx context.Context, ctr string, interact bool, cmd []string) (string, error)
	// Stop and remove a container instance
	StopAndRemoveContainer(ctx context.Context, ctr string) error
}

type CuttlefishContainerManagerOpts struct {
	SockAddr string
}

type CuttlefishContainerManagerImpl struct {
	cli *client.Client
}

func NewCuttlefishContainerManager(opts CuttlefishContainerManagerOpts) (*CuttlefishContainerManagerImpl, error) {
	cliopts := []client.Opt{
		client.WithAPIVersionNegotiation(),
	}
	if opts.SockAddr != "" {
		cliopts = append(cliopts, client.WithHost(opts.SockAddr))
	} else {
		cliopts = append(cliopts, client.FromEnv)
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
	listRes, err := m.cli.ImageList(ctx, image.ListOptions{})
	if err != nil {
		return false, fmt.Errorf("failed to list docker images: %w", err)
	}
	for _, image := range listRes {
		for _, tag := range image.RepoTags {
			if tag == name {
				return true, nil
			}
		}
	}
	return false, nil
}

func (m *CuttlefishContainerManagerImpl) PullImage(ctx context.Context, name string) error {
	reader, err := m.cli.ImagePull(ctx, name, image.PullOptions{})
	if err != nil {
		return fmt.Errorf("failed to request pulling docker image %q: %w", name, err)
	}
	defer reader.Close()
	// Caller of ImagePull should handle its output to complete actual ImagePull operation.
	// Details in https://pkg.go.dev/github.com/docker/docker/client#Client.ImagePull.
	if _, err := io.Copy(io.Discard, reader); err != nil {
		return fmt.Errorf("failed to pull docker image %q: %w", name, err)
	}
	return nil
}

func (m *CuttlefishContainerManagerImpl) CreateAndStartContainer(ctx context.Context, additionalConfig *container.Config, additionalHostConfig *container.HostConfig, name string) (string, error) {
	config := &container.Config{
		AttachStdin: true,
		Tty:         true,
	}
	if additionalConfig != nil {
		if err := mergo.Merge(config, additionalConfig, mergo.WithAppendSlice, mergo.WithOverride); err != nil {
			return "", fmt.Errorf("failed to merge container configuration: %w", err)
		}
	}
	hostConfig := &container.HostConfig{
		CapAdd: []string{"NET_ADMIN"},
		Resources: container.Resources{
			Devices: []container.DeviceMapping{
				{
					CgroupPermissions: "rwm",
					PathInContainer:   "/dev/kvm",
					PathOnHost:        "/dev/kvm",
				},
				{
					CgroupPermissions: "rwm",
					PathInContainer:   "/dev/net/tun",
					PathOnHost:        "/dev/net/tun",
				},
				{
					CgroupPermissions: "rwm",
					PathInContainer:   "/dev/vhost-net",
					PathOnHost:        "/dev/vhost-net",
				},
				{
					CgroupPermissions: "rwm",
					PathInContainer:   "/dev/vhost-vsock",
					PathOnHost:        "/dev/vhost-vsock",
				},
			},
		},
	}
	if additionalHostConfig != nil {
		if err := mergo.Merge(hostConfig, additionalHostConfig, mergo.WithAppendSlice, mergo.WithOverride); err != nil {
			return "", fmt.Errorf("failed to merge container host configuration: %w", err)
		}
	}
	createRes, err := m.cli.ContainerCreate(ctx, config, hostConfig, nil, nil, name)
	if err != nil {
		return "", fmt.Errorf("failed to create docker container: %w", err)
	}
	if err := m.cli.ContainerStart(ctx, createRes.ID, container.StartOptions{}); err != nil {
		return "", fmt.Errorf("failed to start docker container: %w", err)
	}
	return createRes.ID, nil
}

func (m *CuttlefishContainerManagerImpl) ExecOnContainer(ctx context.Context, ctr string, interact bool, cmd []string) (string, error) {
	execConfig := container.ExecOptions{
		AttachStderr: interact,
		AttachStdin:  interact,
		AttachStdout: true,
		Cmd:          cmd,
		Tty:          false,
	}
	createRes, err := m.cli.ContainerExecCreate(ctx, ctr, execConfig)
	if err != nil {
		return "", fmt.Errorf("failed to create container execution %q: %w", strings.Join(cmd, " "), err)
	}
	attachRes, err := m.cli.ContainerExecAttach(ctx, createRes.ID, container.ExecStartOptions{})
	if err != nil {
		return "", fmt.Errorf("failed to attach container execution %q: %w", strings.Join(cmd, " "), err)
	}
	waitCh := make(chan struct{})
	go func() {
		defer close(waitCh)
		if _, err := io.Copy(attachRes.Conn, os.Stdin); err != nil {
			log.Printf("failed to propagate standard input: %v", err)
		}
	}()
	var stdoutBuf bytes.Buffer
	go func() {
		defer close(waitCh)
		var stdout io.Writer
		if interact {
			stdout = io.MultiWriter(os.Stdout, &stdoutBuf)
		} else {
			stdout = &stdoutBuf
		}
		if _, err := stdcopy.StdCopy(stdout, os.Stderr, attachRes.Reader); err != nil {
			log.Printf("failed to propagate standard output: %v", err)
		}
	}()
	<-waitCh
	attachRes.Close()
	if result, err := m.cli.ContainerExecInspect(ctx, createRes.ID); err != nil {
		return "", fmt.Errorf("failed to run command on the container: %w", err)
	} else if result.ExitCode != 0 {
		return "", fmt.Errorf("failed to run command on the container with exit code %d", result.ExitCode)
	}
	return stdoutBuf.String(), nil
}

func (m *CuttlefishContainerManagerImpl) StopAndRemoveContainer(ctx context.Context, ctr string) error {
	if err := m.cli.ContainerStop(ctx, ctr, container.StopOptions{}); err != nil {
		return fmt.Errorf("failed to stop docker container: %w", err)
	}
	if err := m.cli.ContainerRemove(ctx, ctr, container.RemoveOptions{}); err != nil {
		return fmt.Errorf("failed to remove docker container: %w", err)
	}
	return nil
}

func RootlessPodmanSocketAddr() string {
	socketPath := filepath.Join(os.Getenv("XDG_RUNTIME_DIR"), "podman/podman.sock")
	return fmt.Sprintf("unix://%s", socketPath)
}
