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
	"context"
	"crypto/tls"
	"fmt"
	"log"
	"math/rand"
	"net"
	"net/http"
	"net/netip"
	"os"
	"os/user"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/google/uuid"

	"github.com/containerd/errdefs"
)

func CreateCuttlefishHost(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	if err := pullContainerImage(ccm); err != nil {
		return err
	}
	ip, err := createAndStartContainer(ccm, cvdArgs)
	if err != nil {
		return err
	}
	if err := ensureOperatorHealthy(ip); err != nil {
		return err
	}
	return nil
}

func DeleteCuttlefishHost(ccm CuttlefishContainerManager, groupName string) error {
	if err := ccm.StopAndRemoveContainer(context.Background(), ContainerName(groupName)); err != nil {
		return fmt.Errorf("failed to stop and remove container: %w", err)
	}
	return nil
}

func CreateToolingHost(ccm CuttlefishContainerManager) error {
	if err := pullContainerImage(ccm); err != nil {
		return err
	}
	if _, err := ccm.GetClient().ContainerInspect(context.Background(), ToolingContainerName); err == nil {
		return nil
	} else if !errdefs.IsNotFound(err) {
		return err
	}
	return createAndStartToolingContainer(ccm)
}

func DeleteToolingHost(ccm CuttlefishContainerManager) error {
	if err := ccm.StopAndRemoveContainer(context.Background(), ToolingContainerName); err == nil {
		return nil
	} else if errdefs.IsNotFound(err) {
		return nil
	} else {
		return fmt.Errorf("failed to stop and remove container: %w", err)
	}
}

func ExecFetchCmdOnDisposableHost(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	if err := pullContainerImage(ccm); err != nil {
		return err
	}
	cvdDataHome, err := cvdDataHome()
	if err != nil {
		return fmt.Errorf("failed to get cvd data home: %w", err)
	}
	if err := os.MkdirAll(cvdDataHome, 0755); err != nil {
		return fmt.Errorf("failed to eusure directory at %q: %w", cvdDataHome, err)
	}
	targetDir := cvdArgs.GetStringFlagValueOnSubCommandArgs("target_directory")
	if targetDir == "" {
		return fmt.Errorf("target_directory is missing")
	}
	if info, err := os.Stat(targetDir); err != nil || !info.IsDir() {
		return fmt.Errorf("target_directory %q doesn't exist", targetDir)
	}
	extraFlags := []string{
		"--label", fmt.Sprintf("%s=%s", labelCreatedBy, valueCreatedBy),
		"-v", fmt.Sprintf("%s:/root/.local/share/cvd:ro", cvdDataHome),
		"-v", fmt.Sprintf("%s:%s:rw", targetDir, targetDir),
	}
	containerID, err := ccm.CreateAndStartContainer(context.Background(), extraFlags, "")
	if err != nil {
		return fmt.Errorf("failed to create and start container: %w", err)
	}
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	if err := ccm.ExecOnContainer(context.Background(), containerID, args, os.Stdin, os.Stdout, os.Stderr); err != nil {
		return fmt.Errorf("failed to execute fetch command on the container: %w", err)
	}
	if err := ccm.StopAndRemoveContainer(context.Background(), containerID); err != nil {
		return fmt.Errorf("failed to stop and remove container: %w", err)
	}
	return nil
}

func pullContainerImage(ccm CuttlefishContainerManager) error {
	if exists, err := ccm.ImageExists(context.Background(), imageName); err != nil {
		return err
	} else if exists {
		return nil
	}
	log.Printf("Pulling container image %q...\n", imageName)
	return ccm.PullImage(context.Background(), imageName)
}

func cvdDataHome() (string, error) {
	if xdgDataHome := os.Getenv("XDG_DATA_HOME"); xdgDataHome != "" {
		return filepath.Join(xdgDataHome, "cvd"), nil
	}
	if home := os.Getenv("HOME"); home != "" {
		return filepath.Join(home, ".local/share", "cvd"), nil
	}
	return "", fmt.Errorf("failed to find cvd data home dir")
}
func appendPortFlags(flags []string, hostIP string, protocol string, portStart int, portEnd int) []string {
	for port := portStart; port <= portEnd; port++ {
		flags = append(flags, "-p", fmt.Sprintf("%s:%d:%d/%s", hostIP, port, port, protocol))
	}
	return flags
}

func lastIPv4Addr(prefix netip.Prefix) netip.Addr {
	addr := prefix.Masked().Addr().As4()
	mask := net.CIDRMask(prefix.Bits(), 32)
	for i := 0; i < 4; i++ {
		addr[i] |= ^mask[i]
	}
	return netip.AddrFrom4(addr)
}

func findAvailableIPv4Addr(groupNameIpAddrMap map[string]string) (string, error) {
	u, err := user.Current()
	if err != nil {
		return "", fmt.Errorf("failed to get current user: %w", err)
	}
	username := u.Username
	cidr, err := readUserCidrFromConfig(username)
	if err != nil {
		return "", fmt.Errorf("failed to read user CIDR from config: %w", err)
	}
	prefix, err := netip.ParsePrefix(cidr)
	if err != nil {
		return "", fmt.Errorf("failed to parse CIDR: %w", err)
	}
	usedIPMap := make(map[string]bool)
	for _, ip := range groupNameIpAddrMap {
		usedIPMap[ip] = true
	}
	usedIPMap[lastIPv4Addr(prefix).String()] = true
	for ip := prefix.Masked().Addr().Next(); prefix.Contains(ip); ip = ip.Next() {
		if !usedIPMap[ip.String()] {
			return ip.String(), nil
		}
	}
	return "", fmt.Errorf("no available IP address found in %q", cidr)
}

func findAvailableGroupName(groupNameIpAddrMap map[string]string) string {
	const prefix = "cvd_"
	maxNum := 0
	for name := range groupNameIpAddrMap {
		if !strings.HasPrefix(name, prefix) {
			continue
		}
		num, err := strconv.Atoi(strings.TrimPrefix(name, prefix))
		if err != nil {
			continue
		}
		maxNum = max(maxNum, num)
	}
	return fmt.Sprintf("%s%d", prefix, maxNum+1)
}

func createAndStartContainer(ccm CuttlefishContainerManager, cvdArgs *CvdArgs) (string, error) {
	commonArgs := cvdArgs.CommonArgs
	attemptID := uuid.New().String()
	cvdDataHome, err := cvdDataHome()
	if err != nil {
		return "", fmt.Errorf("failed to get cvd data home: %w", err)
	}
	if err := os.MkdirAll(cvdDataHome, 0755); err != nil {
		return "", fmt.Errorf("failed to eusure directory at %q: %w", cvdDataHome, err)
	}
	currentDir, err := os.Getwd()
	if err != nil {
		return "", fmt.Errorf("failed to get current directory: %w", err)
	}
	hostOut := os.Getenv("ANDROID_HOST_OUT")
	if hostOut == "" {
		hostOut = currentDir
	}
	productOut := os.Getenv("ANDROID_PRODUCT_OUT")
	if productOut == "" {
		productOut = currentDir
	}
	podcvdRootDir := "/var/tmp/podcvd"
	if err := os.MkdirAll(podcvdRootDir, 0777); err != nil {
		return "", fmt.Errorf("failed to create podcvd root dir: %w", err)
	}
	podcvdHomeDir := filepath.Join(podcvdRootDir, strconv.Itoa(os.Getuid()), attemptID)
	if err := os.MkdirAll(podcvdHomeDir, 0755); err != nil {
		return "", fmt.Errorf("failed to create podcvd home dir: %w", err)
	}

	extraFlags := []string{
		"-e", "ANDROID_HOST_OUT=/host_out",
		"-e", "ANDROID_PRODUCT_OUT=/product_out",
		"-e", "HOME=/podcvd_home",
		"--label", fmt.Sprintf("%s=%s", labelCreatedBy, valueCreatedBy),
		"--label", fmt.Sprintf("%s=%s", labelAttemptID, attemptID),
		"--annotation", "run.oci.keep_original_groups=1",
		"-v", fmt.Sprintf("%s:/host_out:O", hostOut),
		"-v", fmt.Sprintf("%s:/product_out:O", productOut),
		"-v", fmt.Sprintf("%s:/root/.local/share/cvd:ro", cvdDataHome),
		"-v", fmt.Sprintf("%s:/podcvd_home:rw", podcvdHomeDir),
		"--cap-add", "NET_RAW",
		"--pids-limit", "8192",
	}
	clientID := os.Getenv(envClientID)
	if clientID != "" {
		extraFlags = append(extraFlags, "--label", fmt.Sprintf("%s=%s", labelClientID, clientID))
	}

	bindSet := make(map[string]bool)
	bindSet[hostOut+":/host_out"] = true
	bindSet[productOut+":/product_out"] = true
	bindSet[cvdDataHome+":/root/.local/share/cvd"] = true
	bindSet[podcvdHomeDir+":/podcvd_home"] = true

	for _, arg := range cvdArgs.SubCommandArgs {
		path := arg
		if strings.Contains(arg, "=") {
			path = strings.SplitN(arg, "=", 2)[1]
		}

		// 1. Filter out empty strings which would incorrectly mount the CWD
		if strings.TrimSpace(path) == "" {
			continue
		}

		absPath, err := filepath.Abs(path)
		if err != nil {
			continue
		}
		if _, err := os.Stat(absPath); err != nil {
			continue
		}
		// Prevent mounting critical system directories
		if strings.HasPrefix(absPath, "/dev/") || strings.HasPrefix(absPath, "/sys/") || strings.HasPrefix(absPath, "/proc/") {
			continue
		}

		pathsToMount := []string{absPath}

		// 2. Resolve symlinks and track the target file to ensure it's accessible inside
		if realPath, err := filepath.EvalSymlinks(absPath); err == nil && realPath != absPath {
			pathsToMount = append(pathsToMount, realPath)
		}

		// 3. Add to mount configuration while preventing duplicates
		for _, p := range pathsToMount {
			key := fmt.Sprintf("%s:%s", p, p)
			if !bindSet[key] {
				pInfo, err := os.Stat(p)
				if err != nil {
					log.Printf("warning: failed to stat path %q to mount: %v\n", p, err)
					continue
				}
				opt := "ro"
				if pInfo.IsDir() {
					opt = "O"
				}
				extraFlags = append(extraFlags, "-v", fmt.Sprintf("%s:%s:%s", p, p, opt))
				bindSet[key] = true
			}
		}
	}

	var lastErr error
	for retryCount := 0; retryCount < 10; retryCount++ {
		groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm, true)
		if err != nil {
			return "", err
		}
		ip, err := findAvailableIPv4Addr(groupNameIpAddrMap)
		if err != nil {
			return "", err
		}
		attemptFlags := make([]string, len(extraFlags))
		copy(attemptFlags, extraFlags)
		attemptFlags = append(attemptFlags,
			"-p", fmt.Sprintf("%s:%d:%d/tcp", ip, portOperatorHttpsOnHost, portOperatorHttps),
		)
		attemptFlags = appendPortFlags(attemptFlags, ip, "tcp", 6520, 6529)
		attemptFlags = appendPortFlags(attemptFlags, ip, "tcp", 15550, 15599)
		attemptFlags = appendPortFlags(attemptFlags, ip, "udp", 15550, 15599)
		attemptFlags = append(attemptFlags, "--network", fmt.Sprintf("pasta:-a,%s", ip))
		groupName := commonArgs.GroupName
		if groupName != "" {
			if _, exists := groupNameIpAddrMap[groupName]; exists {
				return "", fmt.Errorf("container instance for group name %q already exists", groupName)
			}
		} else {
			groupName = findAvailableGroupName(groupNameIpAddrMap)
		}
		attemptFlags = append(attemptFlags, "--label", fmt.Sprintf("%s=%s", labelGroupName, groupName))
		if _, err := ccm.CreateAndStartContainer(context.Background(), attemptFlags, ContainerName(groupName)); err != nil {
			lastErr = err
			// Cleanup created container if it failed.
			inspectRes, inspectErr := ccm.GetClient().ContainerInspect(context.Background(), ContainerName(groupName))
			if inspectErr == nil && inspectRes.Config.Labels[labelAttemptID] == attemptID {
				ccm.StopAndRemoveContainer(context.Background(), ContainerName(groupName))
			}
			retryInterval := time.Duration(100+rand.Intn(201)) * time.Millisecond // 100-300 ms
			time.Sleep(retryInterval)
			continue
		}
		commonArgs.GroupName = groupName
		return ip, nil
	}
	return "", lastErr
}

func ensureOperatorHealthy(ip string) error {
	client := &http.Client{
		Timeout: time.Second,
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{InsecureSkipVerify: true},
		},
	}
	const retryCount = 10
	const retryInterval = time.Second
	var lastErr error
	for i := 1; i <= retryCount; i++ {
		time.Sleep(retryInterval)
		resp, err := client.Get(fmt.Sprintf("https://%s:%d/devices", ip, portOperatorHttpsOnHost))
		if err != nil {
			lastErr = fmt.Errorf("failed to check health of operator: %w", err)
			continue
		}
		defer resp.Body.Close()
		if resp.StatusCode != http.StatusOK {
			lastErr = fmt.Errorf("operator returned status: %d", resp.StatusCode)
			continue
		}
		return nil
	}
	return lastErr
}

func createAndStartToolingContainer(ccm CuttlefishContainerManager) error {
	cvdDataHome, err := cvdDataHome()
	if err != nil {
		return fmt.Errorf("failed to get cvd data home: %w", err)
	}
	if err := os.MkdirAll(cvdDataHome, 0755); err != nil {
		return fmt.Errorf("failed to eusure directory at %q: %w", cvdDataHome, err)
	}
	extraFlags := []string{
		"--label", fmt.Sprintf("%s=%s", labelCreatedBy, valueCreatedBy),
		"-v", fmt.Sprintf("%s:/root/.local/share/cvd:rw", cvdDataHome),
	}
	if _, err := ccm.CreateAndStartContainer(context.Background(), extraFlags, ToolingContainerName); err != nil {
		return err
	}
	return nil
}
