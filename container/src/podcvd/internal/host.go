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
	"net"
	"net/http"
	"net/netip"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/google/android-cuttlefish/container/src/libcfcontainer"

	"github.com/containerd/errdefs"
	"github.com/docker/docker/api/types/container"
	"github.com/docker/go-connections/nat"
	"github.com/vishvananda/netlink"
)

func CreateCuttlefishHost(ccm libcfcontainer.CuttlefishContainerManager, commonArgs *CvdCommonArgs) error {
	if err := pullContainerImage(ccm); err != nil {
		return err
	}
	ip, err := createAndStartContainer(ccm, commonArgs)
	if err != nil {
		return err
	}
	if err := ensureOperatorHealthy(ip); err != nil {
		return err
	}
	return nil
}

func DeleteCuttlefishHost(ccm libcfcontainer.CuttlefishContainerManager, groupName string) error {
	if err := ccm.StopAndRemoveContainer(context.Background(), ContainerName(groupName)); err != nil {
		return fmt.Errorf("failed to stop and remove container: %w", err)
	}
	return nil
}

func CreateToolingHost(ccm libcfcontainer.CuttlefishContainerManager) error {
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

func DeleteToolingHost(ccm libcfcontainer.CuttlefishContainerManager) error {
	if err := ccm.StopAndRemoveContainer(context.Background(), ToolingContainerName); err == nil {
		return nil
	} else if errdefs.IsNotFound(err) {
		return nil
	} else {
		return fmt.Errorf("failed to stop and remove container: %w", err)
	}
}

func ExecFetchCmdOnDisposableHost(ccm libcfcontainer.CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	if err := pullContainerImage(ccm); err != nil {
		return err
	}
	containerCfg := &container.Config{
		Image: imageName,
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
	containerHostCfg := &container.HostConfig{
		Binds: []string{
			fmt.Sprintf("%s:/root/.local/share/cvd:ro", cvdDataHome),
			fmt.Sprintf("%s:%s:rw", targetDir, targetDir),
		},
	}
	containerID, err := ccm.CreateAndStartContainer(context.Background(), containerCfg, containerHostCfg, "")
	if err != nil {
		return fmt.Errorf("failed to create and start container: %w", err)
	}
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	if _, err := ccm.ExecOnContainer(context.Background(), containerID, true, args); err != nil {
		return fmt.Errorf("failed to execute fetch command on the container: %w", err)
	}
	if err := ccm.StopAndRemoveContainer(context.Background(), containerID); err != nil {
		return fmt.Errorf("failed to stop and remove container: %w", err)
	}
	return nil
}

func ExecHelpCmdOnDisposableHost(ccm libcfcontainer.CuttlefishContainerManager, cvdArgs *CvdArgs) error {
	if err := pullContainerImage(ccm); err != nil {
		return err
	}
	containerCfg := &container.Config{
		Env: []string{
			"ANDROID_HOST_OUT=/host_out",
		},
		Image: imageName,
	}
	currentDir, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("failed to get current directory: %w", err)
	}
	hostOut := os.Getenv("ANDROID_HOST_OUT")
	if hostOut == "" {
		hostOut = currentDir
	}
	containerHostCfg := &container.HostConfig{
		Binds: []string{
			fmt.Sprintf("%s:/host_out:O", hostOut),
		},
	}
	containerID, err := ccm.CreateAndStartContainer(context.Background(), containerCfg, containerHostCfg, "")
	if err != nil {
		return fmt.Errorf("failed to create and start container: %w", err)
	}
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	if _, err := ccm.ExecOnContainer(context.Background(), containerID, true, args); err != nil {
		return fmt.Errorf("failed to execute help command on the container: %w", err)
	}
	if err := ccm.StopAndRemoveContainer(context.Background(), containerID); err != nil {
		return fmt.Errorf("failed to stop and remove container: %w", err)
	}
	return nil
}

func pullContainerImage(ccm libcfcontainer.CuttlefishContainerManager) error {
	if exists, err := ccm.ImageExists(context.Background(), imageName); err != nil {
		return err
	} else if exists {
		return nil
	}
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

func appendPortBindingRange(portMap nat.PortMap, hostIP string, protocol string, portStart int, portEnd int) {
	for port := portStart; port <= portEnd; port++ {
		portMap[nat.Port(fmt.Sprintf("%d/%s", port, protocol))] = []nat.PortBinding{
			{HostIP: hostIP, HostPort: fmt.Sprintf("%d", port)},
		}
	}
}

func findCidr() (string, error) {
	link, err := netlink.LinkByName(ifName)
	if err != nil {
		return "", fmt.Errorf("failed to find interface %q: %w", ifName, err)
	}
	routes, err := netlink.RouteListFiltered(netlink.FAMILY_V4, &netlink.Route{}, netlink.RT_FILTER_TABLE)
	if err != nil {
		return "", fmt.Errorf("failed to list routes: %w", err)
	}
	for _, route := range routes {
		if route.LinkIndex != link.Attrs().Index || route.Dst == nil {
			continue
		}
		return route.Dst.String(), nil
	}
	return "", fmt.Errorf("failed to find route for interface %q", ifName)
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
	cidr, err := findCidr()
	if err != nil {
		return "", fmt.Errorf("failed to find CIDR: %w", err)
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

func createAndStartContainer(ccm libcfcontainer.CuttlefishContainerManager, commonArgs *CvdCommonArgs) (string, error) {
	containerCfg := &container.Config{
		Env: []string{
			"ANDROID_HOST_OUT=/host_out",
			"ANDROID_PRODUCT_OUT=/product_out",
		},
		Image:  imageName,
		Labels: map[string]string{},
	}
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
	pidsLimit := int64(8192)
	containerHostCfg := &container.HostConfig{
		Annotations: map[string]string{"run.oci.keep_original_groups": "1"},
		Binds: []string{
			fmt.Sprintf("%s:/host_out:O", hostOut),
			fmt.Sprintf("%s:/product_out:O", productOut),
			fmt.Sprintf("%s:/root/.local/share/cvd:ro", cvdDataHome),
		},
		CapAdd: []string{"NET_RAW"},
		Resources: container.Resources{
			PidsLimit: &pidsLimit,
		},
	}
	var lastErr error
	const retryCount = 5
	for i := 0; i < retryCount; i++ {
		groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm)
		if err != nil {
			return "", err
		}
		ip, err := findAvailableIPv4Addr(groupNameIpAddrMap)
		if err != nil {
			return "", err
		}
		containerHostCfg.PortBindings = nat.PortMap{}
		appendPortBindingRange(containerHostCfg.PortBindings, ip, "tcp", portOperatorHttps, portOperatorHttps)
		appendPortBindingRange(containerHostCfg.PortBindings, ip, "tcp", 6520, 6529)
		appendPortBindingRange(containerHostCfg.PortBindings, ip, "tcp", 15550, 15599)
		appendPortBindingRange(containerHostCfg.PortBindings, ip, "udp", 15550, 15599)
		containerHostCfg.NetworkMode = container.NetworkMode(fmt.Sprintf("pasta:-a,%s", ip))
		groupName := commonArgs.GroupName
		if groupName == "" {
			groupName = findAvailableGroupName(groupNameIpAddrMap)
		}
		containerCfg.Labels[labelGroupName] = groupName
		if _, err := ccm.CreateAndStartContainer(context.Background(), containerCfg, containerHostCfg, ContainerName(groupName)); err != nil {
			lastErr = err
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
		resp, err := client.Get(fmt.Sprintf("https://%s:%d/devices", ip, portOperatorHttps))
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

func createAndStartToolingContainer(ccm libcfcontainer.CuttlefishContainerManager) error {
	containerCfg := &container.Config{
		Image: imageName,
	}
	cvdDataHome, err := cvdDataHome()
	if err != nil {
		return fmt.Errorf("failed to get cvd data home: %w", err)
	}
	if err := os.MkdirAll(cvdDataHome, 0755); err != nil {
		return fmt.Errorf("failed to eusure directory at %q: %w", cvdDataHome, err)
	}
	containerHostCfg := &container.HostConfig{
		Binds: []string{
			fmt.Sprintf("%s:/root/.local/share/cvd:rw", cvdDataHome),
		},
	}
	if _, err := ccm.CreateAndStartContainer(context.Background(), containerCfg, containerHostCfg, ToolingContainerName); err != nil {
		return err
	}
	return nil
}
