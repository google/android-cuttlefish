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
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"strings"
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

func ParseCvdArgs(allArgs []string) (*CvdArgs, error) {
	fs := flag.NewFlagSet("podcvd", flag.ExitOnError)
	commonArgs := CvdCommonArgs{}
	fs.StringVar(&commonArgs.GroupName, "group_name", "", "Cuttlefish instance group")
	fs.StringVar(&commonArgs.InstanceName, "instance_name", "", "Cuttlefish instance name or names with comma-separated")
	fs.BoolVar(&commonArgs.Help, "help", false, "Print help message")
	fs.StringVar(&commonArgs.Verbosity, "verbosity", "", "Verbosity level of the command")
	fs.Parse(allArgs)
	subcommandArgs := fs.Args()
	if len(subcommandArgs) > 0 {
		var err error
		subcommandArgs[0] = mapSubcommand(subcommandArgs[0])
		if subcommandArgs[0] == "load" {
			subcommandArgs, err = substituteLoadWithCreateArgs(subcommandArgs)
			if err != nil {
				return nil, err
			}
		}
		if subcommandArgs[0] == "create" {
			if configFile := getStringFlagValue(subcommandArgs, "config_file"); configFile != "" {
				commonArgs.GroupName, subcommandArgs, err = extractGroupNameFromConfigFile(subcommandArgs, configFile)
				if err != nil {
					return nil, err
				}
			}
		}
	}
	return &CvdArgs{
		CommonArgs: &commonArgs,
		// Golang's standard library 'flag' stops parsing just before the first
		// non-flag argument. As the command 'cvd' expects only selector and driver
		// options before the subcommand argument, 'subcommandArgs' should be empty
		// or starting with subcommand name.
		SubCommandArgs: subcommandArgs,
		flagSet:        fs,
	}, nil
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

func (a *CvdArgs) HasHelpFlagOnSubCommandArgs() bool {
	helpFlagNames := []string{
		"h",
		"help",
		"helpfull",
		"helpmatch",
		"helpon",
		"helppackage",
		"helpshort",
		"helpxml",
		"version",
	}
	helpFlags := make(map[string]struct{})
	for _, name := range helpFlagNames {
		helpFlags["-"+name] = struct{}{}
		helpFlags["--"+name] = struct{}{}
	}
	for _, arg := range a.SubCommandArgs {
		if _, exists := helpFlags[strings.Split(arg, "=")[0]]; exists {
			return true
		}
	}
	return false
}

func (a *CvdArgs) GetStringFlagValueOnSubCommandArgs(flagName string) string {
	return getStringFlagValue(a.SubCommandArgs, flagName)
}

func (a *CvdArgs) ReplaceFlagValueOnSubCommandArgs(flagName, newValue string) {
	flags := make(map[string]struct{})
	flags["-"+flagName] = struct{}{}
	flags["--"+flagName] = struct{}{}

	for idx, arg := range a.SubCommandArgs {
		if _, exists := flags[arg]; exists {
			if idx+1 < len(a.SubCommandArgs) && !strings.HasPrefix(a.SubCommandArgs[idx+1], "-") {
				a.SubCommandArgs[idx+1] = newValue
				return
			}
			a.SubCommandArgs[idx] = fmt.Sprintf("-%s=%s", flagName, newValue)
			return
		}
		splitArg := strings.SplitN(arg, "=", 2)
		if len(splitArg) == 2 {
			if _, exists := flags[splitArg[0]]; exists {
				a.SubCommandArgs[idx] = fmt.Sprintf("%s=%s", splitArg[0], newValue)
				return
			}
		}
	}

	a.SubCommandArgs = append(a.SubCommandArgs, fmt.Sprintf("-%s=%s", flagName, newValue))
}

func mapSubcommand(subcmd string) string {
	aliases := map[string]string{
		"fetch_cvd":          "fetch",
		"host_bugreport":     "bugreport",
		"cvd_host_bugreport": "bugreport",
		"stop_cvd":           "stop",
		"rm":                 "remove",
		"launch_cvd":         "start",
		"cvd_status":         "status",
	}
	if mapped, exists := aliases[subcmd]; exists {
		return mapped
	}
	return subcmd
}

func substituteLoadWithCreateArgs(subcmdArgs []string) ([]string, error) {
	subcmdArgs[0] = "create"
	for idx := 1; idx < len(subcmdArgs); idx++ {
		arg := subcmdArgs[idx]
		if strings.HasPrefix(arg, "-") {
			if !strings.Contains(arg, "=") {
				idx++
			}
			continue
		}
		subcmdArgs[idx] = "--config_file=" + arg
		return subcmdArgs, nil
	}
	return nil, fmt.Errorf("missing config file path for load command")
}

type cvdConfigCommon struct {
	GroupName string `json:"group_name"`
}

type cvdConfigFile struct {
	Common cvdConfigCommon `json:"common"`
}

func extractGroupNameFromConfigFile(args []string, configFile string) (string, []string, error) {
	flags := make(map[string]struct{})
	flags["-override"] = struct{}{}
	flags["--override"] = struct{}{}

	prefix := "common.group_name:"
	for idx, arg := range args {
		if _, exists := flags[arg]; exists && idx+1 < len(args) && strings.HasPrefix(args[idx+1], prefix) {
			return strings.TrimPrefix(args[idx+1], prefix), append(args[:idx], args[idx+2:]...), nil
		}
		splitArg := strings.SplitN(arg, "=", 2)
		if len(splitArg) != 2 {
			continue
		}
		if _, exists := flags[splitArg[0]]; exists && strings.HasPrefix(splitArg[1], prefix) {
			return strings.TrimPrefix(splitArg[1], prefix), append(args[:idx], args[idx+1:]...), nil
		}
	}

	absPath := resolveHostPath(configFile)
	if absPath == "" {
		return "", nil, fmt.Errorf("failed to resolve config file path %q", configFile)
	}
	data, err := os.ReadFile(absPath)
	if err != nil {
		return "", nil, fmt.Errorf("failed to read config file %q: %w", absPath, err)
	}
	var config cvdConfigFile
	if err := json.Unmarshal(data, &config); err != nil {
		return "", nil, fmt.Errorf("failed to parse JSON object: %w", err)
	}
	return strings.TrimSpace(config.Common.GroupName), args, nil
}

func getStringFlagValue(args []string, flagName string) string {
	flags := make(map[string]struct{})
	flags["-"+flagName] = struct{}{}
	flags["--"+flagName] = struct{}{}

	for idx, arg := range args {
		if _, exists := flags[arg]; exists && idx+1 < len(args) {
			return args[idx+1]
		}
		splitArg := strings.SplitN(arg, "=", 2)
		if len(splitArg) != 2 {
			continue
		}
		if _, exists := flags[splitArg[0]]; exists {
			return splitArg[1]
		}
	}
	return ""
}
