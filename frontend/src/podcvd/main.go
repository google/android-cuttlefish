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

	"github.com/google/android-cuttlefish/frontend/src/podcvd/internal"
)

func main() {
	cvdArgs := internal.ParseCvdArgs()
	if len(cvdArgs.SubCommandArgs) == 0 {
		// TODO(seungjaeyoo): Support execution without any subcommand
		log.Fatal("execution without any subcommand is not implemented yet")
	}
	subcommand := cvdArgs.SubCommandArgs[0]
	switch subcommand {
	case "bugreport", "create", "display", "env", "powerbtn", "powerwash", "restart", "resume", "screen_recording", "snapshot_take", "start", "status", "suspend":
		// These are supported subcommands on podcvd.
	default:
		// TODO(seungjaeyoo): Support other subcommands of cvd as well.
		log.Fatalf("subcommand %q is not implemented yet", subcommand)
	}

	ccm, err := internal.CuttlefishContainerManager()
	if err != nil {
		log.Fatal(err)
	}
	switch subcommand {
	case "create":
		if err := internal.CreateCuttlefishHost(ccm, cvdArgs.CommonArgs); err != nil {
			log.Fatal(err)
		}
	default:
		// TODO(seungjaeyoo): Validate group name argument.
	}

	args := append([]string{"cvd"}, cvdArgs.SerializeCommonArgs()...)
	args = append(args, cvdArgs.SubCommandArgs...)
	stdout, err := ccm.ExecOnContainer(context.Background(), internal.ContainerName(cvdArgs.CommonArgs.GroupName), args)
	if err != nil {
		log.Fatal(err)
	}

	switch subcommand {
	case "create", "start":
		instanceGroup, err := internal.ParseInstanceGroup(stdout, cvdArgs.CommonArgs.GroupName)
		if err != nil {
			log.Fatal(err)
		}
		if err := internal.ConnectOrDisconnectAdb(ccm, *instanceGroup, true); err != nil {
			log.Fatal(err)
		}
	}
}
