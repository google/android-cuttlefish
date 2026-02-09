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
	"crypto/sha256"
	"fmt"

	"github.com/google/android-cuttlefish/frontend/src/libcfcontainer"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/api/types/filters"
)

func CuttlefishContainerManager() (libcfcontainer.CuttlefishContainerManager, error) {
	ccmOpts := libcfcontainer.CuttlefishContainerManagerOpts{
		SockAddr: libcfcontainer.RootlessPodmanSocketAddr(),
	}
	return libcfcontainer.NewCuttlefishContainerManager(ccmOpts)
}

func Ipv4AddressesByGroupNames(ccm libcfcontainer.CuttlefishContainerManager) (map[string]string, error) {
	opts := container.ListOptions{
		Filters: filters.NewArgs(filters.Arg("label", labelGroupName)),
	}
	containers, err := ccm.GetClient().ContainerList(context.Background(), opts)
	if err != nil {
		return nil, fmt.Errorf("failed to list containers: %w", err)
	}
	groupNameIpAddrMap := make(map[string]string)
	for _, container := range containers {
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
