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
	"flag"
	"fmt"
	"os"
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
