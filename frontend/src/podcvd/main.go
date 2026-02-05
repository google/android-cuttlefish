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
	"crypto/tls"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/exec"
	"time"

	"github.com/google/android-cuttlefish/frontend/src/libcfcontainer"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/go-connections/nat"
)

const (
	imageName         = "us-docker.pkg.dev/android-cuttlefish-artifacts/cuttlefish-orchestration/cuttlefish-orchestration:stable"
	portOperatorHttps = 1443
)

type CvdCommonArgs struct {
	GroupName    string
	InstanceName string
	Help         bool
	Verbosity    string
}

type CvdArgs struct {
	CommonArgs     *CvdCommonArgs
	SubCommandArgs []string
	flagSet        *flag.FlagSet
}

func ParseCvdArgs() *CvdArgs {
	fs := flag.NewFlagSet("podcvd", flag.ExitOnError)
	commonArgs := CvdCommonArgs{}
	fs.StringVar(&commonArgs.GroupName, "group_name", "", "Cuttlefish instance group")
	fs.StringVar(&commonArgs.InstanceName, "instance_name", "", "Cuttlefish instance name or names with comma-separated")
	fs.BoolVar(&commonArgs.Help, "help", false, "Print help message")
	fs.StringVar(&commonArgs.Verbosity, "verbosity", "", "Verbosity level of the command")
	fs.Parse(os.Args[1:])
	return &CvdArgs{
		CommonArgs: &commonArgs,
		// Golang's standard library 'flag' stops parsing just before the first
		// non-flag argument. As the command 'cvd' expects only selector and driver
		// options before the subcommand argument, 'subcommandArgs' should be empty
		// or starting with subcommand name.
		SubCommandArgs: fs.Args(),
		flagSet:        fs,
	}
}

func (a *CvdArgs) SerializeCommonArgs() []string {
	var args []string
	a.flagSet.VisitAll(func(f *flag.Flag) {
		if f.Value.String() != f.DefValue {
			args = append(args, fmt.Sprintf("--%s=%s", f.Name, f.Value.String()))
		}
	})
	return args
}

func cuttlefishContainerManager() (libcfcontainer.CuttlefishContainerManager, error) {
	ccmOpts := libcfcontainer.CuttlefishContainerManagerOpts{
		SockAddr: libcfcontainer.RootlessPodmanSocketAddr(),
	}
	return libcfcontainer.NewCuttlefishContainerManager(ccmOpts)
}

func appendPortBindingRange(portMap nat.PortMap, protocol string, portStart int, portEnd int) {
	for port := portStart; port <= portEnd; port++ {
		portMap[nat.Port(fmt.Sprintf("%d/%s", port, protocol))] = []nat.PortBinding{
			{HostPort: fmt.Sprintf("%d", port)},
		}
	}
}

func createAndStartContainer(ccm libcfcontainer.CuttlefishContainerManager) (string, error) {
	containerCfg := &container.Config{
		Env: []string{
			"ANDROID_HOST_OUT=/host_out",
			"ANDROID_PRODUCT_OUT=/product_out",
		},
		Image: imageName,
	}
	pidsLimit := int64(8192)
	containerHostCfg := &container.HostConfig{
		Annotations: map[string]string{"run.oci.keep_original_groups": "1"},
		Binds: []string{
			fmt.Sprintf("%s:/host_out:O", os.Getenv("ANDROID_HOST_OUT")),
			fmt.Sprintf("%s:/product_out:O", os.Getenv("ANDROID_PRODUCT_OUT")),
		},
		CapAdd:       []string{"NET_RAW"},
		PortBindings: nat.PortMap{},
		Resources: container.Resources{
			PidsLimit: &pidsLimit,
		},
	}
	appendPortBindingRange(containerHostCfg.PortBindings, "tcp", portOperatorHttps, portOperatorHttps)
	appendPortBindingRange(containerHostCfg.PortBindings, "tcp", 6520, 6529)
	appendPortBindingRange(containerHostCfg.PortBindings, "tcp", 15550, 15599)
	appendPortBindingRange(containerHostCfg.PortBindings, "udp", 15550, 15599)
	return ccm.CreateAndStartContainer(context.Background(), containerCfg, containerHostCfg, "")
}

func ensureOperatorHealthy() error {
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
		resp, err := client.Get(fmt.Sprintf("https://localhost:%d/devices", portOperatorHttps))
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

func prepareCuttlefishHost(ccm libcfcontainer.CuttlefishContainerManager) (string, error) {
	if err := ccm.PullImage(context.Background(), imageName); err != nil {
		return "", err
	}
	id, err := createAndStartContainer(ccm)
	if err != nil {
		return "", err
	}
	if err := ensureOperatorHealthy(); err != nil {
		return "", err
	}
	return id, nil
}

func parseAdbPorts(stdout string) ([]int, error) {
	type Instance struct {
		AdbPort int `json:"adb_port"`
	}
	type InstanceGroup struct {
		Instances []Instance `json:"instances"`
	}
	var instanceGroup InstanceGroup
	if err := json.Unmarshal([]byte(stdout), &instanceGroup); err != nil {
		return nil, err
	}
	var ports []int
	for _, instance := range instanceGroup.Instances {
		ports = append(ports, instance.AdbPort)
	}
	return ports, nil
}

func establishAdbConnection(ports ...int) error {
	adbBin, err := exec.LookPath("adb")
	if err != nil {
		return fmt.Errorf("failed to find adb: %w", err)
	}
	if err := exec.Command(adbBin, "start-server").Run(); err != nil {
		return fmt.Errorf("failed to start server: %w", err)
	}
	for _, port := range ports {
		if err := exec.Command(adbBin, "connect", fmt.Sprintf("localhost:%d", port)).Run(); err != nil {
			return fmt.Errorf("failed to connect to Cuttlefish device: %w", err)
		}
	}
	return nil
}

func main() {
	// Parse selector and driver options before the subcommand argument only.
	// TODO(seungjaeyoo): Handle selector/driver options properly for
	// supporting multiple container instances.
	cvdArgs := ParseCvdArgs()
	if len(cvdArgs.SubCommandArgs) == 0 {
		// TODO(seungjaeyoo): Support execution without any subcommand
		log.Fatal("execution without any subcommand is not implemented yet")
	}
	subcommand := cvdArgs.SubCommandArgs[0]
	if subcommand != "create" {
		// TODO(seungjaeyoo): Support other subcommands of cvd as well.
		log.Fatalf("subcommand %q is not implemented yet", subcommand)
	}

	ccm, err := cuttlefishContainerManager()
	if err != nil {
		log.Fatal(err)
	}
	// TODO(seungjaeyoo): Prepare a new Cuttlefish host only when it's required.
	id, err := prepareCuttlefishHost(ccm)
	if err != nil {
		log.Fatal(err)
	}

	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	stdout, err := ccm.ExecOnContainer(context.Background(), id, args)
	if err != nil {
		log.Fatal(err)
	}
	// TODO(seungjaeyoo): Establish ADB connection only when it's required.
	ports, err := parseAdbPorts(stdout)
	if err != nil {
		log.Fatal(err)
	}
	if err := establishAdbConnection(ports...); err != nil {
		log.Fatal(err)
	}
}
