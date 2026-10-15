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
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"

	"github.com/google/uuid"
)

func Main(args []string) error {
	cvdArgs, err := ParseCvdArgs(args)
	if err != nil {
		return err
	}
	if len(cvdArgs.SubCommandArgs) == 0 {
		cvdArgs.SubCommandArgs = []string{"help"}
	}

	ccm := NewCuttlefishContainerManager()

	subcommand := cvdArgs.SubCommandArgs[0]
	if cvdArgs.HasHelpFlagOnSubCommandArgs() {
		cvdArgs.SubCommandArgs = []string{subcommand, "--help"}
		return handleToolingSubcommands(ccm, cvdArgs)
	}
	if subcommand != "setup" {
		if err := CheckDeviceAccessible(); err != nil {
			return err
		}
	}
	switch subcommand {
	case "bugreport", "clone", "create", "display", "env", "logs", "monitor", "powerbtn", "powerwash", "remove", "restart", "resume", "screen_recording", "snapshot_take", "start", "status", "stop", "suspend":
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
	case "ps":
		if err := psAllCuttlefishHosts(ccm); err != nil {
			return err
		}
	case "cache", "help", "lint", "login", "version":
		if err := handleToolingSubcommands(ccm, cvdArgs); err != nil {
			return err
		}
	case "fetch":
		if err := ExecFetchCmdOnDisposableHost(ccm, cvdArgs); err != nil {
			return err
		}
	case "setup":
		return setupPodcvd()
	default:
		return fmt.Errorf("unknown subcommand %q", subcommand)
	}
	return nil
}

func findInstanceGroup(ccm CuttlefishContainerManager, groupName string) (*InstanceGroup, error) {
	var stdoutBuf bytes.Buffer
	if err := ccm.ExecOnContainer(context.Background(), ContainerName(groupName), []string{"cvd", "fleet"}, nil, &stdoutBuf, nil); err != nil {
		return nil, err
	}
	instanceGroup, err := ParseInstanceGroups(stdoutBuf.String(), lookupInnerGroupName(ccm, groupName))
	if err != nil {
		return nil, err
	}
	instanceGroup.GroupName = groupName
	return instanceGroup, nil
}

func disconnectAdb(ccm CuttlefishContainerManager, groupName string) error {
	instanceGroup, err := findInstanceGroup(ccm, groupName)
	if err != nil {
		return err
	}
	return DisconnectAdb(ccm, *instanceGroup)
}

func podcvdBaseDirForGroup(ccm CuttlefishContainerManager, groupName string) (string, error) {
	containerInfo, err := ccm.InspectContainer(context.Background(), ContainerName(groupName))
	if err != nil {
		return "", fmt.Errorf("failed to inspect container: %w", err)
	}
	attemptID := containerInfo.Config.Labels[labelAttemptID]
	podcvdBaseDir := containerInfo.Config.Labels[labelBaseDir]
	if podcvdBaseDir == "" {
		podcvdBaseDir = filepath.Join("/var/tmp/podcvd", strconv.Itoa(os.Getuid()), attemptID)
	}
	return podcvdBaseDir, nil
}

// lookupInnerGroupName returns the group name known by cvd in the group container.
func lookupInnerGroupName(ccm CuttlefishContainerManager, groupName string) string {
	containerInfo, err := ccm.InspectContainer(context.Background(), ContainerName(groupName))
	if err != nil {
		return groupName
	}
	if innerGroupName := containerInfo.Config.Labels[labelInnerGroupName]; innerGroupName != "" {
		return innerGroupName
	}
	return groupName
}

func resolveSnapshotForRestore(cvdArgs *CvdArgs, snapshotsDir string) error {
	hostSnapshotPath, exists := cvdArgs.GetStringFlagValueOnSubCommandArgs("snapshot_path")
	if !exists || hostSnapshotPath == "" {
		return nil
	}
	resolvedPath, err := filepath.EvalSymlinks(hostSnapshotPath)
	if err != nil {
		return fmt.Errorf("failed to resolve snapshot path %q: %w", hostSnapshotPath, err)
	}
	absResolvedPath, err := filepath.Abs(resolvedPath)
	if err != nil {
		return fmt.Errorf("failed to resolve absolute path for %q: %w", resolvedPath, err)
	}
	resolvedSnapshotsDir, err := filepath.EvalSymlinks(snapshotsDir)
	if err != nil {
		return fmt.Errorf("failed to resolve snapshots dir %q: %w", snapshotsDir, err)
	}
	rel, err := filepath.Rel(resolvedSnapshotsDir, absResolvedPath)
	if err != nil || rel == "." || rel == ".." || strings.HasPrefix(rel, ".."+string(filepath.Separator)) {
		return fmt.Errorf("snapshot path %q (resolved to %q) is outside snapshots dir %q", hostSnapshotPath, absResolvedPath, snapshotsDir)
	}
	containerSnapshotPath := filepath.Join(containerSnapshotsDir, rel)
	cvdArgs.ReplaceFlagValueOnSubCommandArgs("snapshot_path", containerSnapshotPath)
	return nil
}

func handleCreateOrStartExecution(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	_, hasConfigFile := cvdArgs.GetStringFlagValueOnSubCommandArgs("config_file")
	if hasConfigFile {
		cvdArgs.ReplaceFlagValueOnSubCommandArgs("base_directory", "/podcvd_base")
	}
	if cvdArgs.SubCommandArgs[0] == "start" {
		if err := resolveSnapshotForRestore(cvdArgs, hostSnapshotsDir()); err != nil {
			return err
		}
	}
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	if hasConfigFile {
		args = append(args, fmt.Sprintf("--override=common.group_name:%s", cvdArgs.CommonArgs.GroupName))
	}

	groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm, false, false)
	if err != nil {
		return fmt.Errorf("failed to get IPv4 addresses for group names: %w", err)
	}
	ip, exists := groupNameIpAddrMap[cvdArgs.CommonArgs.GroupName]
	if !exists {
		return fmt.Errorf("failed to find IPv4 address for group name %q", cvdArgs.CommonArgs.GroupName)
	}
	rewriter := NewStderrRewriter(os.Stderr, ip)
	defer rewriter.Flush()
	var stdoutBuf bytes.Buffer
	if err := ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, &stdoutBuf, rewriter); err != nil {
		return err
	}
	var instanceGroup *InstanceGroup
	if format, exists := cvdArgs.GetStringFlagValueOnSubCommandArgs("print_group_format"); exists && format == "human" {
		os.Stdout.Write(stdoutBuf.Bytes())
		group, err := findInstanceGroup(ccm, cvdArgs.CommonArgs.GroupName)
		if err != nil {
			return err
		}
		instanceGroup = group
	} else {
		var res map[string]any
		if err := json.Unmarshal(stdoutBuf.Bytes(), &res); err != nil {
			return fmt.Errorf("failed to unmarshal json: %w", err)
		}
		RenameCvdGroupJsonRaw(res, cvdArgs.CommonArgs.InnerGroupName, cvdArgs.CommonArgs.GroupName)
		podcvdBaseDir, err := podcvdBaseDirForGroup(ccm, cvdArgs.CommonArgs.GroupName)
		if err != nil {
			return err
		}
		UpdateCvdGroupJsonRaw(res, podcvdBaseDir, ip)
		stdout, err := json.MarshalIndent(res, "", "        ")
		if err != nil {
			return fmt.Errorf("failed to marshal json: %w", err)
		}
		os.Stdout.Write(stdout)
		instanceGroup, err = ParseInstanceGroup(string(stdout), cvdArgs.CommonArgs.GroupName)
		if err != nil {
			return err
		}
	}
	return ConnectAdb(ccm, *instanceGroup)
}

func handleBugreportExecution(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	hostOutputPath, exists := cvdArgs.GetStringFlagValueOnSubCommandArgs("output")
	if !exists {
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

func handleSnapshotTakeExecution(ccm CuttlefishContainerManager, cvdArgs *CvdArgs, snapshotsDir string) error {
	hostSnapshotPath, exists := cvdArgs.GetStringFlagValueOnSubCommandArgs("snapshot_path")
	if !exists || hostSnapshotPath == "" {
		args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
		args = append(args, cvdArgs.SubCommandArgs...)
		return ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, os.Stdout, os.Stderr)
	}
	absHostSnapshotPath, err := filepath.Abs(hostSnapshotPath)
	if err != nil {
		return fmt.Errorf("failed to resolve absolute path for %q: %w", hostSnapshotPath, err)
	}
	_, lstatErr := os.Lstat(absHostSnapshotPath)
	if lstatErr != nil && !os.IsNotExist(lstatErr) {
		return fmt.Errorf("failed to stat snapshot path %q: %w", absHostSnapshotPath, lstatErr)
	}
	if err := os.MkdirAll(filepath.Dir(absHostSnapshotPath), 0755); err != nil {
		return fmt.Errorf("failed to create parent directory for %q: %w", absHostSnapshotPath, err)
	}
	if err := os.MkdirAll(snapshotsDir, 0755); err != nil {
		return fmt.Errorf("failed to create snapshots dir: %w", err)
	}
	snapshotID := uuid.New().String()
	hostStagingPath := filepath.Join(snapshotsDir, snapshotID)
	containerSnapshotPath := filepath.Join(containerSnapshotsDir, snapshotID)
	succeeded := false
	defer func() {
		if !succeeded {
			_ = os.RemoveAll(hostStagingPath)
		}
	}()

	if lstatErr == nil {
		// Pre-create hostStagingPath so snapshot_util_cvd inside the container
		// natively enforces --force when absHostSnapshotPath already exists.
		if err := os.MkdirAll(hostStagingPath, 0755); err != nil {
			return fmt.Errorf("failed to create staging snapshot dir %q: %w", hostStagingPath, err)
		}
	}

	cvdArgs.ReplaceFlagValueOnSubCommandArgs("snapshot_path", containerSnapshotPath)
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	var stderrBuf bytes.Buffer
	err = ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, os.Stdout, &stderrBuf)
	if stderrBuf.Len() > 0 {
		_, _ = os.Stderr.WriteString(strings.ReplaceAll(stderrBuf.String(), containerSnapshotPath, hostSnapshotPath))
	}
	if err != nil {
		return fmt.Errorf("failed to execute cvd snapshot_take in the container: %w", err)
	}
	if lstatErr == nil {
		if oldTarget, linkErr := os.Readlink(absHostSnapshotPath); linkErr == nil {
			if !filepath.IsAbs(oldTarget) {
				oldTarget = filepath.Join(filepath.Dir(absHostSnapshotPath), oldTarget)
			}
			if rel, relErr := filepath.Rel(snapshotsDir, oldTarget); relErr == nil && rel != "." && rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator)) {
				_ = os.RemoveAll(oldTarget)
			}
		}
		if err := os.RemoveAll(absHostSnapshotPath); err != nil {
			return fmt.Errorf("failed to remove existing snapshot path %q: %w", absHostSnapshotPath, err)
		}
	}
	if err := os.Symlink(hostStagingPath, absHostSnapshotPath); err != nil {
		return fmt.Errorf("failed to create snapshot symlink at %q: %w", absHostSnapshotPath, err)
	}
	succeeded = true
	return nil
}

func handleCloneExecution(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	groupName := cvdArgs.CommonArgs.GroupName
	innerGroupName := cvdArgs.CommonArgs.InnerGroupName
	containerName := ContainerName(groupName)
	mountSpecs, err := cuttlefishHostMountSpecs(ccm, groupName)
	if err != nil {
		return err
	}
	podcvdBaseDir, err := podcvdBaseDirForGroup(ccm, groupName)
	if err != nil {
		return err
	}
	snapshotID := uuid.New().String()
	snapshotArgs := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	snapshotArgs = append(snapshotArgs, "snapshot_take", "--auto_suspend", "--snapshot_path="+filepath.Join(containerSnapshotsDir, snapshotID))
	if err := ccm.ExecOnContainer(context.Background(), containerName, snapshotArgs, nil, os.Stderr, os.Stderr); err != nil {
		return fmt.Errorf("failed to take snapshot of group %q: %w", groupName, err)
	}
	if err := disconnectAdb(ccm, groupName); err != nil {
		return err
	}
	stopArgs := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	stopArgs = append(stopArgs, "stop")
	if err := ccm.ExecOnContainer(context.Background(), containerName, stopArgs, nil, os.Stderr, os.Stderr); err != nil {
		return fmt.Errorf("failed to stop group %q: %w", groupName, err)
	}
	image := cloneImageRepo + ":" + snapshotID
	var cloneGroupName string
	cloneErr := ccm.CommitContainer(context.Background(), containerName, image)
	if cloneErr == nil {
		// Create the clone while the group is stopped, as it copies the group's home.
		cloneGroupName, cloneErr = CreateClonedCuttlefishHost(ccm, image, innerGroupName, podcvdBaseDir, mountSpecs)
	}
	hostSnapshotPath := filepath.Join(hostSnapshotsDir(), snapshotID)
	// Restart the source group even if cloning failed.
	if err := startGroupFromSnapshot(ccm, groupName, innerGroupName, hostSnapshotPath); err != nil {
		return errors.Join(cloneErr, err)
	}
	if cloneErr != nil {
		return cloneErr
	}
	return startGroupFromSnapshot(ccm, cloneGroupName, innerGroupName, hostSnapshotPath)
}

func startGroupFromSnapshot(ccm CuttlefishContainerManager, groupName, innerGroupName, hostSnapshotPath string) error {
	cvdArgs, err := ParseCvdArgs([]string{"--group_name=" + groupName, "start", "--snapshot_path=" + hostSnapshotPath})
	if err != nil {
		return err
	}
	cvdArgs.CommonArgs.InnerGroupName = innerGroupName
	if err := handleCreateOrStartExecution(ccm, cvdArgs); err != nil {
		return fmt.Errorf("failed to start group %q from snapshot %q: %w", groupName, hostSnapshotPath, err)
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
	_, hasPrint := cvdArgs.GetStringFlagValueOnSubCommandArgs("print")
	_, hasP := cvdArgs.GetStringFlagValueOnSubCommandArgs("p")
	if hasPrint || hasP {
		return ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, os.Stdout, os.Stderr)
	}

	var stdoutBuf bytes.Buffer
	if err := ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), args, os.Stdin, &stdoutBuf, os.Stderr); err != nil {
		return err
	}
	podcvdBaseDir, err := podcvdBaseDirForGroup(ccm, cvdArgs.CommonArgs.GroupName)
	if err != nil {
		return err
	}
	regex := regexp.MustCompile(`/var/tmp/cvd/[0-9]+/[0-9]+`)
	translatedOutput := regex.ReplaceAllString(stdoutBuf.String(), podcvdBaseDir)
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
			groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm, false, false)
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
		cvdArgs.CommonArgs.InnerGroupName = lookupInnerGroupName(ccm, cvdArgs.CommonArgs.GroupName)
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
	case "clone":
		return handleCloneExecution(ccm, cvdArgs)
	case "snapshot_take":
		return handleSnapshotTakeExecution(ccm, cvdArgs, hostSnapshotsDir())
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
	groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm, true, false)
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
	if err := pruneCloneImages(ccm); err != nil {
		errs = append(errs, fmt.Errorf("failed to remove clone images: %w", err))
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

	results, err := ExecOnAllCuttlefishHosts(ccm, []string{"cvd", "fleet"}, nil)
	if err != nil {
		return err
	}

	containers, err := ccm.ListContainers(context.Background(), false)
	if err != nil {
		return fmt.Errorf("failed to list containers: %w", err)
	}
	podcvdBaseDirMap := make(map[string]string)
	uid := strconv.Itoa(os.Getuid())
	for _, c := range containers {
		podcvdBaseDir := c.Labels[labelBaseDir]
		if podcvdBaseDir == "" {
			podcvdBaseDir = filepath.Join("/var/tmp/podcvd", uid, c.Labels[labelAttemptID])
		}
		if groupName, ok := c.Labels[labelGroupName]; ok {
			podcvdBaseDirMap[groupName] = podcvdBaseDir
		}
	}

	combinedRes := cvdFleetResponse{
		Groups: []any{},
	}
	for _, res := range results {
		var fleetRes cvdFleetResponse
		if err := json.Unmarshal(res.Stdout, &fleetRes); err != nil {
			return err
		}
		for idx := range fleetRes.Groups {
			RenameCvdGroupJsonRaw(fleetRes.Groups[idx], res.InnerGroupName, res.GroupName)
			UpdateCvdGroupJsonRaw(fleetRes.Groups[idx], podcvdBaseDirMap[res.GroupName], res.IP)
		}
		combinedRes.Groups = append(combinedRes.Groups, fleetRes.Groups...)
	}
	combinedOutput, err := json.MarshalIndent(combinedRes, "", "        ")
	if err != nil {
		return err
	}
	os.Stdout.Write(append(combinedOutput, '\n'))
	return nil
}

func psAllCuttlefishHosts(ccm CuttlefishContainerManager) error {
	results, err := ExecOnAllCuttlefishHosts(ccm, []string{"cvd", "ps"}, os.Stderr)
	if err != nil {
		return err
	}
	if len(results) > 0 {
		return PrintPsOutputs(results, os.Stdout)
	}
	if err := CreateToolingHost(ccm); err != nil {
		return err
	}
	return ccm.ExecOnContainer(context.Background(), ToolingContainerName, []string{"cvd", "ps"}, nil, os.Stdout, os.Stderr)
}

func handleToolingSubcommands(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	if err := CreateToolingHost(ccm); err != nil {
		return err
	}
	subcommand := cvdArgs.SubCommandArgs[0]
	switch subcommand {
	case "cache":
		if err := handleCacheExecution(ccm, cvdArgs); err != nil {
			return err
		}
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

func handleCacheExecution(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	cacheDir := hostCacheDir()

	if len(cvdArgs.SubCommandArgs) > 1 && cvdArgs.SubCommandArgs[1] == "empty" {
		entries, err := os.ReadDir(cacheDir)
		if err != nil && !os.IsNotExist(err) {
			return fmt.Errorf("failed to empty cache directory %q: %w", cacheDir, err)
		}
		for _, entry := range entries {
			if err := os.RemoveAll(filepath.Join(cacheDir, entry.Name())); err != nil {
				return fmt.Errorf("failed to empty cache directory %q: %w", cacheDir, err)
			}
		}
		fmt.Printf("Cache at %q has been emptied\n", cacheDir)
		return nil
	}

	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	var stdoutBuf bytes.Buffer
	if err := ccm.ExecOnContainer(context.Background(), ToolingContainerName, args, os.Stdin, &stdoutBuf, os.Stderr); err != nil {
		return err
	}
	translatedOutput := strings.ReplaceAll(stdoutBuf.String(), "/var/tmp/cvd/0/cache", cacheDir)
	_, err := os.Stdout.WriteString(translatedOutput)
	return err
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

func setupPodcvd() error {
	cmd := exec.Command("podcvd-setup")
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to execute 'podcvd-setup': %w", err)
	}
	return nil
}
