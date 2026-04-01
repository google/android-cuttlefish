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
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"sync"

	"github.com/google/android-cuttlefish/container/src/libcfcontainer"
)

func Main(args []string) {
	cvdArgs := ParseCvdArgs(args)
	if len(cvdArgs.SubCommandArgs) == 0 {
		cvdArgs.SubCommandArgs = []string{"help"}
	}

	ccm, err := CuttlefishContainerManager()
	if err != nil {
		log.Fatal(err)
	}

	subcommand := cvdArgs.SubCommandArgs[0]
	if cvdArgs.HasHelpFlagOnSubCommandArgs() {
		switch subcommand {
		case "cache", "clear", "create", "display", "env", "fetch", "fleet", "help", "lint", "load", "login", "powerbtn", "powerwash", "remove", "reset", "restart", "resume", "screen_recording", "snapshot_take", "status", "stop", "suspend", "version":
			cvdArgs.SubCommandArgs = []string{subcommand, "--help"}
			if err := handleToolingSubcommands(ccm, cvdArgs); err != nil {
				log.Fatal(err)
			}
		case "bugreport", "start":
			cvdArgs.SubCommandArgs = []string{subcommand, "--help"}
			if err := ExecHelpCmdOnDisposableHost(ccm, cvdArgs); err != nil {
				log.Fatal(err)
			}
		default:
			log.Fatalf("unknown subcommand %q", subcommand)
		}
	} else {
		if err := CheckDeviceAccessible(); err != nil {
			log.Fatal(err)
		}
		switch subcommand {
		case "bugreport", "create", "display", "env", "powerbtn", "powerwash", "remove", "restart", "resume", "screen_recording", "snapshot_take", "start", "status", "stop", "suspend":
			if err := handleSubcommandsForSingleInstanceGroup(ccm, cvdArgs); err != nil {
				log.Fatal(err)
			}
		case "clear", "reset":
			if err := clearAllCuttlefishHosts(ccm); err != nil {
				log.Fatal(err)
			}
		case "fleet":
			if err := fleetAllCuttlefishHosts(ccm); err != nil {
				log.Fatal(err)
			}
		case "help", "login", "version":
			if err := handleToolingSubcommands(ccm, cvdArgs); err != nil {
				log.Fatal(err)
			}
		case "fetch":
			if err := ExecFetchCmdOnDisposableHost(ccm, cvdArgs); err != nil {
				log.Fatal(err)
			}
		case "cache", "lint":
			// TODO(seungjaeyoo): Support other subcommands of cvd as well.
			log.Fatalf("subcommand %q is not implemented yet", subcommand)
		default:
			log.Fatalf("unknown subcommand %q", subcommand)
		}
	}
}

func disconnectAdb(ccm libcfcontainer.CuttlefishContainerManager, groupName string) error {
	stdout, err := ccm.ExecOnContainer(context.Background(), ContainerName(groupName), false, []string{"cvd", "fleet"})
	if err != nil {
		return err
	}
	instanceGroup, err := ParseInstanceGroups(stdout, groupName)
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
			groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm)
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
	stdout, err := ccm.ExecOnContainer(context.Background(), ContainerName(cvdArgs.CommonArgs.GroupName), true, args)
	if err != nil {
		return err
	}
	switch subcommand {
	case "create", "start":
		instanceGroup, err := ParseInstanceGroup(stdout, cvdArgs.CommonArgs.GroupName)
		if err != nil {
			return err
		}
		if err := ConnectAdb(ccm, *instanceGroup); err != nil {
			return err
		}
	}
	return nil
}

func clearAllCuttlefishHosts(ccm libcfcontainer.CuttlefishContainerManager) error {
	groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm)
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
		errCh <- DeleteToolingHost(ccm)
	}()
	wg.Wait()
	close(errCh)
	errs := []error{}
	for err := range errCh {
		errs = append(errs, err)
	}
	return errors.Join(errs...)
}

func fleetAllCuttlefishHosts(ccm libcfcontainer.CuttlefishContainerManager) error {
	type cvdFleetResponse struct {
		Groups []json.RawMessage `json:"groups"`
	}

	groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm)
	if err != nil {
		return fmt.Errorf("failed to get IPv4 addresses for group names: %w", err)
	}
	var wg sync.WaitGroup
	wg.Add(len(groupNameIpAddrMap))
	resCh := make(chan cvdFleetResponse, len(groupNameIpAddrMap))
	errCh := make(chan error, len(groupNameIpAddrMap))
	for groupName := range groupNameIpAddrMap {
		go func() {
			defer wg.Done()
			stdout, err := ccm.ExecOnContainer(context.Background(), ContainerName(groupName), false, []string{"cvd", "fleet"})
			if err != nil {
				errCh <- err
				return
			}
			var res cvdFleetResponse
			errCh <- json.Unmarshal([]byte(stdout), &res)
			resCh <- res
		}()
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
	if _, err := ccm.ExecOnContainer(context.Background(), ToolingContainerName, true, args); err != nil {
		return err
	}
	return nil
}
