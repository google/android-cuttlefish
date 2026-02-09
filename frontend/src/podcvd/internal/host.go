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
	"strconv"
	"strings"
	"time"

	"github.com/google/android-cuttlefish/frontend/src/libcfcontainer"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/go-connections/nat"
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

func pullContainerImage(ccm libcfcontainer.CuttlefishContainerManager) error {
	if exists, err := ccm.ImageExists(context.Background(), imageName); err != nil {
		return err
	} else if exists {
		return nil
	}
	return ccm.PullImage(context.Background(), imageName)
}

func appendPortBindingRange(portMap nat.PortMap, hostIP string, protocol string, portStart int, portEnd int) {
	for port := portStart; port <= portEnd; port++ {
		portMap[nat.Port(fmt.Sprintf("%d/%s", port, protocol))] = []nat.PortBinding{
			{HostIP: hostIP, HostPort: fmt.Sprintf("%d", port)},
		}
	}
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
	prefix, err := netip.ParsePrefix(containerInstanceSubnet)
	if err != nil {
		return "", err
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
	return "", fmt.Errorf("no available IP address found in %q", containerInstanceSubnet)
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
	pidsLimit := int64(8192)
	containerHostCfg := &container.HostConfig{
		Annotations: map[string]string{"run.oci.keep_original_groups": "1"},
		Binds: []string{
			fmt.Sprintf("%s:/host_out:O", os.Getenv("ANDROID_HOST_OUT")),
			fmt.Sprintf("%s:/product_out:O", os.Getenv("ANDROID_PRODUCT_OUT")),
		},
		CapAdd: []string{"NET_RAW"},
		Resources: container.Resources{
			PidsLimit: &pidsLimit,
		},
	}
	var err error
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
		var groupName string
		if commonArgs.GroupName == "" {
			groupName = findAvailableGroupName(groupNameIpAddrMap)
		} else {
			groupName = commonArgs.GroupName
		}
		containerCfg.Labels[labelGroupName] = groupName
		if _, err := ccm.CreateAndStartContainer(context.Background(), containerCfg, containerHostCfg, ContainerName(groupName)); err == nil {
			commonArgs.GroupName = groupName
			return ip, nil
		}
	}
	return "", err
}

func ensureOperatorHealthy(ip string) error {
	client := &http.Client{
		Timeout: time.Second,
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{InsecureSkipVerify: true},
		},
	}
	const retryCount = 5
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
