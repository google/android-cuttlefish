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
	"bufio"
	"context"
	"crypto/sha256"
	"fmt"
	"os"
	"strings"

	"github.com/google/android-cuttlefish/container/src/libcfcontainer"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/api/types/filters"
)

func CuttlefishContainerManager() (libcfcontainer.CuttlefishContainerManager, error) {
	ccmOpts := libcfcontainer.CuttlefishContainerManagerOpts{
		SockAddr: libcfcontainer.RootlessPodmanSocketAddr(),
	}
	return libcfcontainer.NewCuttlefishContainerManager(ccmOpts)
}

func Ipv4AddressesByGroupNames(ccm libcfcontainer.CuttlefishContainerManager, allContainers bool) (map[string]string, error) {
	opts := container.ListOptions{
		Filters: filters.NewArgs(
			filters.Arg("label", labelGroupName),
			filters.Arg("label", fmt.Sprintf("%s=%s", labelCreatedBy, valueCreatedBy)),
		),
		All: allContainers,
	}
	var containers []container.Summary
	var lastErr error
	for retryCount := 0; retryCount < 10; retryCount++ {
		containers, lastErr = ccm.GetClient().ContainerList(context.Background(), opts)
		if lastErr == nil {
			break
		}
	}
	if lastErr != nil {
		return nil, fmt.Errorf("failed to list containers: %w", lastErr)
	}
	groupNameIpAddrMap := make(map[string]string)
	clientID := os.Getenv(envClientID)
	for _, container := range containers {
		if !allContainers && clientID != "" {
			if val, exists := container.Labels[labelClientID]; exists && val != clientID {
				continue
			}
		}
		groupName, exists := container.Labels[labelGroupName]
		if !exists {
			continue
		}
		for _, port := range container.Ports {
			if port.PrivatePort == portOperatorHttps {
				groupNameIpAddrMap[groupName] = port.IP
				break
			}
		}
		if _, exists := groupNameIpAddrMap[groupName]; !exists {
			return nil, fmt.Errorf("failed to get IPv4 address for group name %q", groupName)
		}
	}
	return groupNameIpAddrMap, nil
}

func ContainerName(groupName string) string {
	return fmt.Sprintf("%x", sha256.Sum256([]byte(groupName)))[:12]
}

func readUserCidrFromConfig(username string) (string, error) {
	file, err := os.Open("/etc/podcvd.users")
	if err != nil {
		return "", err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.SplitN(line, ":", 2)
		if len(parts) == 2 && parts[0] == username {
			return parts[1], nil
		}
	}
	if err := scanner.Err(); err != nil {
		return "", err
	}
	return "", fmt.Errorf("user %q not found in /etc/podcvd.users", username)
}

