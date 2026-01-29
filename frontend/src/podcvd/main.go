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

package main

import (
	"context"
	"fmt"
	"log"
	"os"

	"github.com/google/android-cuttlefish/frontend/src/libcfcontainer"

	"github.com/docker/docker/api/types/container"
)

const (
	imageName = "us-docker.pkg.dev/android-cuttlefish-artifacts/cuttlefish-orchestration/cuttlefish-orchestration:stable"
)

func cuttlefishContainerManager() (libcfcontainer.CuttlefishContainerManager, error) {
	ccmOpts := libcfcontainer.CuttlefishContainerManagerOpts{
		SockAddr: libcfcontainer.RootlessPodmanSocketAddr(),
	}
	return libcfcontainer.NewCuttlefishContainerManager(ccmOpts)
}

func prepareCuttlefishHost(ccm libcfcontainer.CuttlefishContainerManager) (string, error) {
	ctx := context.Background()
	if err := ccm.PullImage(ctx, imageName); err != nil {
		return "", err
	}
	containerCfg := &container.Config{
		Env: []string{
			"ANDROID_HOST_OUT=/host_out",
			"ANDROID_PRODUCT_OUT=/product_out",
		},
		Image: imageName,
	}
	pidsLimit := int64(8192)
	containerHostCfg := &container.HostConfig{
		Binds: []string{
			fmt.Sprintf("%s:/host_out:O", os.Getenv("ANDROID_HOST_OUT")),
			fmt.Sprintf("%s:/product_out:O", os.Getenv("ANDROID_PRODUCT_OUT")),
		},
		CapAdd:      []string{"NET_RAW"},
		NetworkMode: container.NetworkMode("pasta:--host-lo-to-ns-lo,-t,1443,-t,6520-6529,-t,15550-15599,-u,15550-15599"),
		Resources: container.Resources{
			PidsLimit: &pidsLimit,
		},
	}
	return ccm.CreateAndStartContainer(ctx, containerCfg, containerHostCfg, "")
}

func main() {
	ccm, err := cuttlefishContainerManager()
	if err != nil {
		log.Fatal(err)
	}
	if _, err := prepareCuttlefishHost(ccm); err != nil {
		log.Fatal(err)
	}
}
