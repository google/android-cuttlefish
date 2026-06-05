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
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"

	"github.com/google/uuid"
)

func Main(args []string) error {
	cvdArgs := ParseCvdArgs(args)
	if len(cvdArgs.SubCommandArgs) == 0 {
		cvdArgs.SubCommandArgs = []string{"help"}
	}

	ccm, err := NewCuttlefishContainerManager()
	if err != nil {
		return err
	}

	subcommand := cvdArgs.SubCommandArgs[0]
	if cvdArgs.HasHelpFlagOnSubCommandArgs() {
		cvdArgs.SubCommandArgs = []string{subcommand, "--help"}
		return handleToolingSubcommands(ccm, cvdArgs)
	}
	if err := CheckDeviceAccessible(); err != nil {
		return err
	}
	switch subcommand {
	case "bugreport", "create", "display", "env", "logs", "powerbtn", "powerwash", "remove", "restart", "resume", "screen_recording", "snapshot_take", "start", "status", "stop", "suspend":
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
	case "help", "lint", "login", "version":
		if err := handleToolingSubcommands(ccm, cvdArgs); err != nil {
			return err
		}
	case "fetch":
		if err := ExecFetchCmdOnDisposableHost(ccm, cvdArgs); err != nil {
			return err
		}
	case "cache", "load", "monitor", "setup":
		// TODO(seungjaeyoo): Support other subcommands of cvd as well.
		return fmt.Errorf("subcommand %q is not implemented yet", subcommand)
	default:
		return fmt.Errorf("unknown subcommand %q", subcommand)
	}
	return nil
}

func disconnectAdb(ccm CuttlefishContainerManager, groupName string) error {
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

func handleCreateOrStartExecution(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)

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
	containerInfo, err := ccm.InspectContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName))
	if err != nil {
		return fmt.Errorf("failed to inspect container: %w", err)
	}
	attemptID := containerInfo.Config.Labels["attempt_id"]
	podcvdHomeDir := filepath.Join("/var/tmp/podcvd", strconv.Itoa(os.Getuid()), attemptID)
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
	return nil
}

func handleBugreportExecution(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	hostOutputPath := cvdArgs.GetStringFlagValueOnSubCommandArgs("output")
	if hostOutputPath == "" {
		hostOutputPath = "host_bugreport.zip"
	}
	absHostOutputPath, err := filepath.Abs(hostOutputPath)
	if err != nil {
		return fmt.Errorf("failed to resolve absolute path for %q: %w", hostOutputPath, err)
	}
	containerOutputPath := filepath.Join("/tmp", fmt.Sprintf("bugreport-%s.zip", uuid.New().String()))
	cvdArgs.ReplaceFlagValueOnSubCommandArgs("output", containerOutputPath)
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	if err := ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, os.Stdout, os.Stderr); err != nil {
		return fmt.Errorf("failed to execute cvd bugreport in the container: %w", err)
	}
	defer ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), []string{"rm", containerOutputPath}, nil, nil, nil)
	err = ccm.CopyFromContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), containerOutputPath, absHostOutputPath)
	if err != nil {
		return fmt.Errorf("failed to copy bugreport from container: %w", err)
	}
	return nil
}

func formatLogsList(output string) string {
	lines := strings.Split(output, "\n")
	for i, line := range lines {
		parts := strings.SplitN(line, " ", 2)
		if len(parts) == 2 && strings.HasPrefix(parts[1], "/") {
			lines[i] = fmt.Sprintf("%-29s %s", parts[0], parts[1])
		}
	}
	return strings.Join(lines, "\n")
}

func handleLogsExecution(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	if cvdArgs.GetStringFlagValueOnSubCommandArgs("print") != "" || cvdArgs.GetStringFlagValueOnSubCommandArgs("p") != "" {
		return ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, os.Stdout, os.Stderr)
	}

	var stdoutBuf bytes.Buffer
	if err := ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, &stdoutBuf, os.Stderr); err != nil {
		return err
	}
	containerInfo, err := ccm.InspectContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName))
	if err != nil {
		return fmt.Errorf("failed to inspect container: %w", err)
	}
	attemptID := containerInfo.Config.Labels["attempt_id"]
	podcvdHomeDir := filepath.Join("/var/tmp/podcvd", strconv.Itoa(os.Getuid()), attemptID)
	regex := regexp.MustCompile(`/var/tmp/cvd/[0-9]+/[0-9]+/home`)
	translatedOutput := regex.ReplaceAllString(stdoutBuf.String(), podcvdHomeDir)
	if Isatty(os.Stdout.Fd()) {
		translatedOutput = formatLogsList(translatedOutput)
	}
	_, err = os.Stdout.WriteString(translatedOutput)
	return err
}

func handleSubcommandsForSingleInstanceGroup(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	subcommand := cvdArgs.SubCommandArgs[0]
	switch subcommand {
	case "create":
		if err := CreateCuttlefishHost(ccm, cvdArgs); err != nil {
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
	switch subcommand {
	case "create", "start":
		return handleCreateOrStartExecution(ccm, cvdArgs)
	case "bugreport":
		return handleBugreportExecution(ccm, cvdArgs)
	case "logs":
		return handleLogsExecution(ccm, cvdArgs)
	default:
		args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
		args = append(args, cvdArgs.SubCommandArgs...)
		if err := ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, os.Stdout, os.Stderr); err != nil {
			return err
		}
	}
	return nil
}

func clearAllCuttlefishHosts(ccm CuttlefishContainerManager) error {
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
	uidDir := filepath.Join("/var/tmp/podcvd", strconv.Itoa(os.Getuid()))
	if err := os.RemoveAll(uidDir); err != nil {
		errs = append(errs, fmt.Errorf("failed to remove uid dir: %w", err))
	}
	return errors.Join(errs...)
}

func fleetAllCuttlefishHosts(ccm CuttlefishContainerManager) error {
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
			containerInfo, err := ccm.InspectContainer(context.Background(), containerName)
			if err != nil {
				errCh <- err
				return
			}
			attemptID := containerInfo.Config.Labels["attempt_id"]
			podcvdHomeDir := filepath.Join("/var/tmp/podcvd", strconv.Itoa(os.Getuid()), attemptID)
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
	combinedRes := cvdFleetResponse{
		Groups: []any{},
	}
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

func handleToolingSubcommands(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	if err := CreateToolingHost(ccm); err != nil {
		return err
	}
	subcommand := cvdArgs.SubCommandArgs[0]
	switch subcommand {
	case "lint":
		if err := handleLintExecution(ccm, cvdArgs); err != nil {
			return err
		}
	default:
		args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
		args = append(args, cvdArgs.SubCommandArgs...)
		if err := ccm.ExecOnContainer(context.Background(), ToolingContainerName, args, os.Stdin, os.Stdout, os.Stderr); err != nil {
			return err
		}
	}
	return nil
}

func handleLintExecution(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	if len(cvdArgs.SubCommandArgs) < 2 {
		return fmt.Errorf("missing JSON config file path")
	}
	configPath := cvdArgs.SubCommandArgs[1]
	file, err := os.Open(configPath)
	if err != nil {
		return fmt.Errorf("failed to open config file %q: %w", configPath, err)
	}
	defer file.Close()
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, "lint", "/dev/stdin")
	var stdoutBuf bytes.Buffer
	if err := ccm.ExecOnContainer(context.Background(), ToolingContainerName, args, file, &stdoutBuf, os.Stderr); err != nil {
		return err
	}
	output := strings.ReplaceAll(stdoutBuf.String(), "/dev/stdin", configPath)
	os.Stdout.WriteString(output)
	return nil
}
