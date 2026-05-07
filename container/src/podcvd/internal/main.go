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
	"errors"
	"fmt"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strconv"
	"sync"
	"syscall"
	"time"

	"github.com/google/android-cuttlefish/container/src/libcfcontainer"
	"github.com/google/uuid"
)

func Main(args []string) error {
	cvdArgs := ParseCvdArgs(args)
	if len(cvdArgs.SubCommandArgs) == 0 {
		cvdArgs.SubCommandArgs = []string{"help"}
	}

	podcvdSockDir := filepath.Join(podcvdRootDir, "sock")
	if err := os.MkdirAll(podcvdSockDir, 0777); err != nil {
		return fmt.Errorf("failed to create podcvd root dir: %w", err)
	}
	sockfilePath := filepath.Join(podcvdSockDir, fmt.Sprintf("podcvd_%s.sock", uuid.New().String()))
	socketPath := fmt.Sprintf("unix://%s", sockfilePath)
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	cmd := exec.Command("podman", "system", "service", "--time=0", socketPath)
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start podman system service: %w", err)
	}
	defer os.Remove(sockfilePath)
	defer cmd.Process.Kill()
	go func() {
		<-sigChan
		cmd.Process.Kill()
		os.Remove(sockfilePath)
		os.Exit(0)
	}()
	if err := waitSocketRunning(sockfilePath); err != nil {
		return err
	}
	os.Setenv("DOCKER_HOST", socketPath)
	ccm, err := CuttlefishContainerManager(socketPath)
	if err != nil {
		return err
	}

	subcommand := cvdArgs.SubCommandArgs[0]
	if cvdArgs.HasHelpFlagOnSubCommandArgs() {
		switch subcommand {
		case "cache", "clear", "create", "display", "env", "fetch", "fleet", "help", "lint", "load", "login", "powerbtn", "powerwash", "remove", "reset", "restart", "resume", "screen_recording", "snapshot_take", "status", "stop", "suspend", "version":
			cvdArgs.SubCommandArgs = []string{subcommand, "--help"}
			if err := handleToolingSubcommands(ccm, cvdArgs); err != nil {
				return err
			}
		case "bugreport", "start":
			cvdArgs.SubCommandArgs = []string{subcommand, "--help"}
			if err := ExecHelpCmdOnDisposableHost(ccm, cvdArgs); err != nil {
				return err
			}
		default:
			return fmt.Errorf("unknown subcommand %q", subcommand)
		}
	} else {
		if err := CheckDeviceAccessible(); err != nil {
			return err
		}
		switch subcommand {
		case "bugreport", "create", "display", "env", "powerbtn", "powerwash", "remove", "restart", "resume", "screen_recording", "snapshot_take", "start", "status", "stop", "suspend":
			if err := handleSubcommandsForSingleInstanceGroup(ccm, cvdArgs); err != nil {
				return err
			}
		case "clear", "reset":
			if err := clearAllCuttlefishHosts(ccm); err != nil {
				return err
			}
		case "fleet":
			if err := fleetAllCuttlefishHosts(ccm); err != nil {
				return err
			}
		case "help", "login", "version":
			if err := handleToolingSubcommands(ccm, cvdArgs); err != nil {
				return err
			}
		case "fetch":
			if err := ExecFetchCmdOnDisposableHost(ccm, cvdArgs); err != nil {
				return err
			}
		case "cache", "lint":
			// TODO(seungjaeyoo): Support other subcommands of cvd as well.
			return fmt.Errorf("subcommand %q is not implemented yet", subcommand)
		default:
			return fmt.Errorf("unknown subcommand %q", subcommand)
		}
	}
	return nil
}

func waitSocketRunning(path string) error {
	start := time.Now()
	timeout := time.Second
	for time.Since(start) < timeout {
		if _, err := net.Dial("unix", path); err == nil {
			return nil
		}
		time.Sleep(1 * time.Millisecond)
	}
	return fmt.Errorf("timed out waiting for podman socket to be ready")
}

func disconnectAdb(ccm libcfcontainer.CuttlefishContainerManager, groupName string) error {
	var stdoutBuf bytes.Buffer
	if err := ccm.ExecOnContainer(context.Background(), ContainerName(groupName), []string{"cvd", "fleet"}, nil, &stdoutBuf, nil); err != nil {
		return err
	}
	instanceGroup, err := ParseInstanceGroups(stdoutBuf.String(), groupName)
	if err != nil {
		return err
	}
	return DisconnectAdb(ccm, *instanceGroup)
}

func handleSubcommandsForSingleInstanceGroup(ccm libcfcontainer.CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	subcommand := cvdArgs.SubCommandArgs[0]
	switch subcommand {
	case "create":
		if err := CreateCuttlefishHost(ccm, cvdArgs.CommonArgs); err != nil {
			return err
		}
	default:
		if cvdArgs.CommonArgs.GroupName == "" {
			groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm, false)
			if err != nil {
				return fmt.Errorf("failed to get IPv4 addresses for group names: %w", err)
			}
			if len(groupNameIpAddrMap) != 1 {
				// TODO(seungjaeyoo): Support to select group name from the terminal.
				return fmt.Errorf("the number of instance groups isn't 1 (actual: %d)", len(groupNameIpAddrMap))
			}
			for groupName := range groupNameIpAddrMap {
				cvdArgs.CommonArgs.GroupName = groupName
				break
			}
		}
	}
	switch subcommand {
	case "remove", "stop":
		if err := disconnectAdb(ccm, cvdArgs.CommonArgs.GroupName); err != nil {
			return err
		}
		// If the subcommand is 'remove', it doesn't need to execute cvd on the
		// container instance as it should be removed in the end.
		if subcommand == "remove" {
			return DeleteCuttlefishHost(ccm, cvdArgs.CommonArgs.GroupName)
		}
	}
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	switch subcommand {
	case "create", "start":
		var stdoutBuf bytes.Buffer
		if err := ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, &stdoutBuf, os.Stderr); err != nil {
			return err
		}
		var res map[string]any
		if err := json.Unmarshal(stdoutBuf.Bytes(), &res); err != nil {
			return fmt.Errorf("failed to unmarshal json: %w", err)
		}
		groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm, false)
		if err != nil {
			return fmt.Errorf("failed to get IPv4 addresses for group names: %w", err)
		}
		ip, exists := groupNameIpAddrMap[cvdArgs.CommonArgs.GroupName]
		if !exists {
			return fmt.Errorf("failed to find IPv4 address for group name %q", cvdArgs.CommonArgs.GroupName)
		}
		inspectRes, err := ccm.GetClient().ContainerInspect(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName))
		if err != nil {
			return fmt.Errorf("failed to inspect container: %w", err)
		}
		attemptID := inspectRes.Config.Labels["attempt_id"]
		podcvdHomeDir := filepath.Join(podcvdRootDir, strconv.Itoa(os.Getuid()), attemptID)
		UpdateCvdGroupJsonRaw(res, podcvdHomeDir, ip)
		stdout, err := json.MarshalIndent(res, "", "        ")
		if err != nil {
			return fmt.Errorf("failed to marshal json: %w", err)
		}
		os.Stdout.Write(stdout)
		instanceGroup, err := ParseInstanceGroup(string(stdout), cvdArgs.CommonArgs.GroupName)
		if err != nil {
			return err
		}
		if err := ConnectAdb(ccm, *instanceGroup); err != nil {
			return err
		}
	default:
		if err := ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, os.Stdout, os.Stderr); err != nil {
			return err
		}
	}
	return nil
}

func clearAllCuttlefishHosts(ccm libcfcontainer.CuttlefishContainerManager) error {
	groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm, true)
	if err != nil {
		return fmt.Errorf("failed to get IPv4 addresses for group names: %w", err)
	}
	var wg sync.WaitGroup
	wg.Add(len(groupNameIpAddrMap) + 1)
	errCh := make(chan error, len(groupNameIpAddrMap)+1)
	for groupName := range groupNameIpAddrMap {
		go func(groupName string) {
			defer wg.Done()
			errCh <- errors.Join(disconnectAdb(ccm, groupName), DeleteCuttlefishHost(ccm, groupName))
		}(groupName)
	}
	go func() {
		defer wg.Done()
		if _, exists := os.LookupEnv(envClientID); !exists {
			errCh <- DeleteToolingHost(ccm)
		}
	}()
	wg.Wait()
	close(errCh)
	errs := []error{}
	for err := range errCh {
		errs = append(errs, err)
	}
	uidDir := filepath.Join(podcvdRootDir, strconv.Itoa(os.Getuid()))
	if err := os.RemoveAll(uidDir); err != nil {
		errs = append(errs, fmt.Errorf("failed to remove uid dir: %w", err))
	}
	return errors.Join(errs...)
}

func fleetAllCuttlefishHosts(ccm libcfcontainer.CuttlefishContainerManager) error {
	type cvdFleetResponse struct {
		Groups []any `json:"groups"`
	}

	groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm, false)
	if err != nil {
		return fmt.Errorf("failed to get IPv4 addresses for group names: %w", err)
	}
	var wg sync.WaitGroup
	wg.Add(len(groupNameIpAddrMap))
	resCh := make(chan cvdFleetResponse, len(groupNameIpAddrMap))
	errCh := make(chan error, len(groupNameIpAddrMap))
	for groupName, ip := range groupNameIpAddrMap {
		go func(groupName, ip string) {
			defer wg.Done()
			containerName := ContainerName(groupName)
			var stdoutBuf bytes.Buffer
			if err := ccm.ExecOnContainer(context.Background(), containerName, []string{"cvd", "fleet"}, nil, &stdoutBuf, nil); err != nil {
				errCh <- err
				return
			}
			var res cvdFleetResponse
			if err := json.Unmarshal(stdoutBuf.Bytes(), &res); err != nil {
				errCh <- err
				return
			}
			inspectRes, err := ccm.GetClient().ContainerInspect(context.Background(), containerName)
			if err != nil {
				errCh <- err
				return
			}
			attemptID := inspectRes.Config.Labels["attempt_id"]
			podcvdHomeDir := filepath.Join(podcvdRootDir, strconv.Itoa(os.Getuid()), attemptID)
			for idx := range res.Groups {
				UpdateCvdGroupJsonRaw(res.Groups[idx], podcvdHomeDir, ip)
			}
			resCh <- res
		}(groupName, ip)
	}
	wg.Wait()
	close(resCh)
	close(errCh)

	var errs []error
	for err := range errCh {
		errs = append(errs, err)
	}
	if err := errors.Join(errs...); err != nil {
		return err
	}
	combinedRes := cvdFleetResponse{}
	for res := range resCh {
		combinedRes.Groups = append(combinedRes.Groups, res.Groups...)
	}
	combinedOutput, err := json.MarshalIndent(combinedRes, "", "        ")
	if err != nil {
		return err
	}
	os.Stdout.Write(append(combinedOutput, '\n'))
	return nil
}

func handleToolingSubcommands(ccm libcfcontainer.CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	if err := CreateToolingHost(ccm); err != nil {
		return err
	}
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	if err := ccm.ExecOnContainer(context.Background(), ToolingContainerName, args, os.Stdin, os.Stdout, os.Stderr); err != nil {
		return err
	}
	return nil
}
