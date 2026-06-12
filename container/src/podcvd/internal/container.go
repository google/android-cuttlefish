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
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"strings"
)

type ContainerConfig struct {
	Labels map[string]string `json:"Labels"`
}

type ContainerInfo struct {
	Config *ContainerConfig `json:"Config"`
}

type PortBinding struct {
	IP          string `json:"host_ip"`
	PrivatePort uint16 `json:"container_port"`
}

type ContainerListEntry struct {
	Labels map[string]string `json:"Labels"`
	Ports  []PortBinding     `json:"Ports"`
}

type CuttlefishContainerManager interface {
	// Check whether an image with the given name exists on the container engine or not
	ImageExists(ctx context.Context, name string) (bool, error)
	// Pull the container image
	PullImage(ctx context.Context, name string) error
	// Check whether a container with the given name exists or not
	ContainerExists(ctx context.Context, name string) (bool, error)
	// Inspect a container to get its information
	InspectContainer(ctx context.Context, name string) (*ContainerInfo, error)
	// Create and start a container instance with raw extra flags
	CreateAndStartContainer(ctx context.Context, extraFlags []string, name string) (string, error)
	// List containers managed by podcvd
	ListContainers(ctx context.Context, all bool) ([]ContainerListEntry, error)
	// Copy files/folders from a container instance
	CopyFromContainer(ctx context.Context, ctr string, srcPath string, dstPath string) error
	// Execute a command on a running container instance
	ExecOnContainer(ctx context.Context, ctr string, cmd []string, stdin io.Reader, stdout io.Writer, stderr io.Writer) error
	// Stop and remove a container instance
	StopAndRemoveContainer(ctx context.Context, ctr string) error
}

type CuttlefishContainerManagerImpl struct{}

func NewCuttlefishContainerManager() CuttlefishContainerManager {
	return &CuttlefishContainerManagerImpl{}
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

func (m *CuttlefishContainerManagerImpl) ContainerExists(ctx context.Context, name string) (bool, error) {
	cmd := exec.CommandContext(ctx, "podman", "container", "exists", name)
	err := cmd.Run()
	if err == nil {
		return true, nil
	}
	if _, ok := err.(*exec.ExitError); ok {
		return false, nil
	}
	return false, fmt.Errorf("failed to check container existence: %w", err)
}

func (m *CuttlefishContainerManagerImpl) InspectContainer(ctx context.Context, name string) (*ContainerInfo, error) {
	cmd := exec.CommandContext(ctx, "podman", "inspect", name)
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("failed to inspect container %q: %s: %w", name, stderr.String(), err)
	}
	var infos []ContainerInfo
	if err := json.Unmarshal(stdout.Bytes(), &infos); err != nil {
		return nil, fmt.Errorf("failed to unmarshal container inspect: %w", err)
	}
	if len(infos) == 0 {
		return nil, fmt.Errorf("no inspect info returned for container %q", name)
	}
	return &infos[0], nil
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
	if hasNvidiaGPU() {
		args = append(args,
			"-e", "NVIDIA_DRIVER_CAPABILITIES=all",
			"--device", "android.com/gpu-podcvd=all",
		)
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

func (m *CuttlefishContainerManagerImpl) ListContainers(ctx context.Context, all bool) ([]ContainerListEntry, error) {
	args := []string{"ps", "--filter", "label=group_name", "--filter", "label=created_by=podcvd", "--format", "json"}
	if all {
		args = append(args, "--all")
	}
	cmd := exec.CommandContext(ctx, "podman", args...)
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("failed to list containers: %s: %w", stderr.String(), err)
	}
	var entries []ContainerListEntry
	if err := json.Unmarshal(stdout.Bytes(), &entries); err != nil {
		return nil, fmt.Errorf("failed to unmarshal container list: %w", err)
	}
	return entries, nil
}

func (m *CuttlefishContainerManagerImpl) CopyFromContainer(ctx context.Context, ctr string, srcPath string, dstPath string) error {
	cmd := exec.CommandContext(ctx, "podman", "cp", ctr+":"+srcPath, dstPath)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to copy from container: %s: %w", stderr.String(), err)
	}
	return nil
}

func (m *CuttlefishContainerManagerImpl) ExecOnContainer(ctx context.Context, ctr string, cmd []string, stdin io.Reader, stdout io.Writer, stderr io.Writer) error {
	args := []string{"exec"}
	if stdin != nil {
		args = append(args, "-i")
	}
	if useTTY(stdin, stdout, stderr) {
		args = append(args, "-t")
	}
	args = append(args, ctr)
	args = append(args, cmd...)
	execCmd := exec.CommandContext(ctx, "podman", args...)
	execCmd.Stdin = stdin
	execCmd.Stdout = stdout
	execCmd.Stderr = stderr
	err := execCmd.Run()
	if err == nil {
		return nil
	}
	if exitErr, ok := err.(*exec.ExitError); ok {
		return fmt.Errorf("failed to run command on the container with exit code %d: %w", exitErr.ExitCode(), err)
	}
	return fmt.Errorf("failed to run command on the container: %w", err)
}

func (m *CuttlefishContainerManagerImpl) StopAndRemoveContainer(ctx context.Context, ctr string) error {
	cmd := exec.CommandContext(ctx, "podman", "rm", "-f", "-i", "-t", "0", ctr)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to stop and remove container %q: %w", ctr, err)
	}
	return nil
}

func useTTY(stdin io.Reader, stdout io.Writer, stderr io.Writer) bool {
	if stdin == nil || stdout == nil || stderr == nil {
		return false
	}
	if f, ok := stdin.(interface{ Fd() uintptr }); !ok || !Isatty(f.Fd()) {
		return false
	}
	if f, ok := stdout.(interface{ Fd() uintptr }); !ok || !Isatty(f.Fd()) {
		return false
	}
	if f, ok := stderr.(interface{ Fd() uintptr }); !ok || !Isatty(f.Fd()) {
		return false
	}
	return true
}

func hasNvidiaGPU() bool {
	if _, err := os.Stat("/dev/nvidiactl"); err != nil {
		return false
	}
	if _, err := os.Stat("/etc/cdi/nvidia-podcvd.yaml"); err != nil {
		return false
	}
	return true
}
