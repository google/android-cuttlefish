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
	"log"

	"github.com/google/android-cuttlefish/frontend/src/libcfcontainer"
	"github.com/google/android-cuttlefish/frontend/src/podcvd/internal"
)

func handleSubcommandsForSingleInstanceGroup(ccm libcfcontainer.CuttlefishContainerManager, cvdArgs *internal.CvdArgs) error {
	subcommand := cvdArgs.SubCommandArgs[0]
	switch subcommand {
	case "create":
		if err := internal.CreateCuttlefishHost(ccm, cvdArgs.CommonArgs); err != nil {
			return err
		}
	default:
		// TODO(seungjaeyoo): Validate group name argument.
	}
	switch subcommand {
	case "remove", "stop":
		args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
		args = append(args, "fleet")
		stdout, err := ccm.ExecOnContainer(context.Background(), internal.ContainerName(cvdArgs.CommonArgs.GroupName), false, args)
		if err != nil {
			return err
		}
		instanceGroup, err := internal.ParseInstanceGroups(stdout, cvdArgs.CommonArgs.GroupName)
		if err != nil {
			return err
		}
		if err := internal.DisconnectAdb(ccm, *instanceGroup); err != nil {
			return err
		}
		// If the subcommand is 'remove', it doesn't need to execute cvd on the
		// container instance as it should be removed in the end.
		if subcommand == "remove" {
			return internal.DeleteCuttlefishHost(ccm, cvdArgs.CommonArgs.GroupName)
		}
	}
	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	stdout, err := ccm.ExecOnContainer(context.Background(), internal.ContainerName(cvdArgs.CommonArgs.GroupName), true, args)
	if err != nil {
		return err
	}
	switch subcommand {
	case "create", "start":
		instanceGroup, err := internal.ParseInstanceGroup(stdout, cvdArgs.CommonArgs.GroupName)
		if err != nil {
			return err
		}
		if err := internal.ConnectAdb(ccm, *instanceGroup); err != nil {
			return err
		}
	}
	return nil
}

func main() {
	cvdArgs := internal.ParseCvdArgs()
	if len(cvdArgs.SubCommandArgs) == 0 {
		// TODO(seungjaeyoo): Support execution without any subcommand
		log.Fatal("execution without any subcommand is not implemented yet")
	}

	ccm, err := internal.CuttlefishContainerManager()
	if err != nil {
		log.Fatal(err)
	}

	subcommand := cvdArgs.SubCommandArgs[0]
	switch subcommand {
	case "bugreport", "create", "display", "env", "powerbtn", "powerwash", "remove", "restart", "resume", "screen_recording", "snapshot_take", "start", "status", "stop", "suspend":
		if err := handleSubcommandsForSingleInstanceGroup(ccm, cvdArgs); err != nil {
			log.Fatal(err)
		}
	default:
		// TODO(seungjaeyoo): Support other subcommands of cvd as well.
		log.Fatalf("subcommand %q is not implemented yet", subcommand)
	}
}
