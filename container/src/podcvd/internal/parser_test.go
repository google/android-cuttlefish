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
	"os"
	"slices"
	"testing"
)

func TestParseCvdArgs(t *testing.T) {
	tests := []struct {
		name               string
		configJSONContent  string
		args               []string
		wantCommonArgs     CvdCommonArgs
		wantSubCommandArgs []string
		wantErr            bool
	}{
		{
			name:               "common args",
			configJSONContent:  "",
			args:               []string{"--group_name=cvd_1", "--instance_name=1", "status"},
			wantCommonArgs:     CvdCommonArgs{GroupName: "cvd_1", InstanceName: "1"},
			wantSubCommandArgs: []string{"status"},
		},
		{
			name:               "subcommand alias",
			configJSONContent:  "",
			args:               []string{"host_bugreport"},
			wantCommonArgs:     CvdCommonArgs{},
			wantSubCommandArgs: []string{"bugreport"},
		},
		{
			name:               "load",
			configJSONContent:  `{}`,
			args:               []string{"load", "config.json"},
			wantCommonArgs:     CvdCommonArgs{},
			wantSubCommandArgs: []string{"create", "--config_file=config.json"},
		},
		{
			name:               "load with flags before config file",
			configJSONContent:  `{}`,
			args:               []string{"load", "--credential_source", "none", "--acquire_file_lock=false", "config.json"},
			wantCommonArgs:     CvdCommonArgs{},
			wantSubCommandArgs: []string{"create", "--credential_source", "none", "--acquire_file_lock=false", "--config_file=config.json"},
		},
		{
			name:              "load without config file",
			configJSONContent: "",
			args:              []string{"load", "--credential_source", "none"},
			wantErr:           true,
		},
		{
			name:               "group name from config file",
			configJSONContent:  `{"common":{"group_name":"cvd_2"}}`,
			args:               []string{"create", "--config_file=config.json"},
			wantCommonArgs:     CvdCommonArgs{GroupName: "cvd_2"},
			wantSubCommandArgs: []string{"create", "--config_file=config.json"},
		},
		{
			name:              "missing config file",
			configJSONContent: "",
			args:              []string{"create", "--config_file=config.json"},
			wantErr:           true,
		},
		{
			name:              "invalid config file",
			configJSONContent: `{"common":`,
			args:              []string{"create", "--config_file=config.json"},
			wantErr:           true,
		},
		{
			name:               "group name from override",
			configJSONContent:  `{}`,
			args:               []string{"create", "--config_file=config.json", "--override=common.group_name:cvd_3"},
			wantCommonArgs:     CvdCommonArgs{GroupName: "cvd_3"},
			wantSubCommandArgs: []string{"create", "--config_file=config.json"},
		},
		{
			name:               "group name from override in space form",
			configJSONContent:  `{}`,
			args:               []string{"create", "--config_file=config.json", "--override", "common.group_name:cvd_4"},
			wantCommonArgs:     CvdCommonArgs{GroupName: "cvd_4"},
			wantSubCommandArgs: []string{"create", "--config_file=config.json"},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			t.Chdir(t.TempDir())
			if tc.configJSONContent != "" {
				if err := os.WriteFile("config.json", []byte(tc.configJSONContent), 0644); err != nil {
					t.Fatal(err)
				}
			}
			got, err := ParseCvdArgs(tc.args)
			if tc.wantErr {
				if err == nil {
					t.Errorf("ParseCvdArgs(%q) succeeded; want error", tc.args)
				}
				return
			}
			if err != nil {
				t.Fatalf("ParseCvdArgs(%q) failed: %v", tc.args, err)
			}
			if *got.CommonArgs != tc.wantCommonArgs {
				t.Errorf("ParseCvdArgs(%q).CommonArgs = %+v; want %+v", tc.args, *got.CommonArgs, tc.wantCommonArgs)
			}
			if !slices.Equal(got.SubCommandArgs, tc.wantSubCommandArgs) {
				t.Errorf("ParseCvdArgs(%q).SubCommandArgs = %q; want %q", tc.args, got.SubCommandArgs, tc.wantSubCommandArgs)
			}
		})
	}
}

func TestSerializeCommonArgs(t *testing.T) {
	tests := []struct {
		name string
		args []string
		want []string
	}{
		{
			name: "no common args",
			args: []string{"fleet"},
			want: nil,
		},
		{
			name: "common args",
			args: []string{"--group_name=cvd_1", "--verbosity=DEBUG", "status"},
			want: []string{"--group_name=cvd_1", "--verbosity=DEBUG"},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			cvdArgs, err := ParseCvdArgs(tc.args)
			if err != nil {
				t.Fatalf("ParseCvdArgs(%q) failed: %v", tc.args, err)
			}
			if got := cvdArgs.SerializeCommonArgs(); !slices.Equal(got, tc.want) {
				t.Errorf("SerializeCommonArgs() = %q; want %q", got, tc.want)
			}
		})
	}
}

func TestHasHelpFlagOnSubCommandArgs(t *testing.T) {
	tests := []struct {
		name           string
		subCommandArgs []string
		want           bool
	}{
		{
			name:           "help flag",
			subCommandArgs: []string{"create", "--help"},
			want:           true,
		},
		{
			name:           "help flag with value",
			subCommandArgs: []string{"fetch", "-helpmatch=build"},
			want:           true,
		},
		{
			name:           "no help flag",
			subCommandArgs: []string{"stop", "--wait_for_launcher=10"},
			want:           false,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			cvdArgs := &CvdArgs{SubCommandArgs: tc.subCommandArgs}
			if got := cvdArgs.HasHelpFlagOnSubCommandArgs(); got != tc.want {
				t.Errorf("HasHelpFlagOnSubCommandArgs() = %v; want %v", got, tc.want)
			}
		})
	}
}

func TestGetStringFlagValueOnSubCommandArgs(t *testing.T) {
	tests := []struct {
		name           string
		subCommandArgs []string
		flagName       string
		wantValue      string
		wantExists     bool
	}{
		{
			name:           "equals form",
			subCommandArgs: []string{"create", "--num_instances=2"},
			flagName:       "num_instances",
			wantValue:      "2",
			wantExists:     true,
		},
		{
			name:           "space form",
			subCommandArgs: []string{"bugreport", "--output", "/tmp/bugreport.zip"},
			flagName:       "output",
			wantValue:      "/tmp/bugreport.zip",
			wantExists:     true,
		},
		{
			name:           "missing",
			subCommandArgs: []string{"stop"},
			flagName:       "wait_for_launcher",
			wantValue:      "",
			wantExists:     false,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			cvdArgs := &CvdArgs{SubCommandArgs: tc.subCommandArgs}
			gotValue, gotExists := cvdArgs.GetStringFlagValueOnSubCommandArgs(tc.flagName)
			if gotValue != tc.wantValue || gotExists != tc.wantExists {
				t.Errorf("GetStringFlagValueOnSubCommandArgs(%q) = %q, %v; want %q, %v", tc.flagName, gotValue, gotExists, tc.wantValue, tc.wantExists)
			}
		})
	}
}

func TestRemoveFlagValueOnSubCommandArgs(t *testing.T) {
	tests := []struct {
		name           string
		subCommandArgs []string
		flagName       string
		want           []string
	}{
		{
			name:           "equals form",
			subCommandArgs: []string{"create", "--num_instances=2", "--daemon"},
			flagName:       "num_instances",
			want:           []string{"create", "--daemon"},
		},
		{
			name:           "space form",
			subCommandArgs: []string{"start", "--cpus", "4", "--memory_mb", "8192"},
			flagName:       "cpus",
			want:           []string{"start", "--memory_mb", "8192"},
		},
		{
			name:           "flag without value",
			subCommandArgs: []string{"snapshot_take", "--force", "--snapshot_path=/tmp/snap"},
			flagName:       "force",
			want:           []string{"snapshot_take", "--snapshot_path=/tmp/snap"},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			cvdArgs := &CvdArgs{SubCommandArgs: tc.subCommandArgs}
			cvdArgs.RemoveFlagValueOnSubCommandArgs(tc.flagName)
			if !slices.Equal(cvdArgs.SubCommandArgs, tc.want) {
				t.Errorf("SubCommandArgs after RemoveFlagValueOnSubCommandArgs(%q) = %q; want %q", tc.flagName, cvdArgs.SubCommandArgs, tc.want)
			}
		})
	}
}

func TestReplaceFlagValueOnSubCommandArgs(t *testing.T) {
	tests := []struct {
		name           string
		subCommandArgs []string
		flagName       string
		value          string
		want           []string
	}{
		{
			name:           "equals form",
			subCommandArgs: []string{"fetch", "--target_directory=/tmp/old"},
			flagName:       "target_directory",
			value:          "/tmp/new",
			want:           []string{"fetch", "-target_directory=/tmp/new"},
		},
		{
			name:           "space form",
			subCommandArgs: []string{"create", "--num_instances", "1", "--daemon"},
			flagName:       "num_instances",
			value:          "2",
			want:           []string{"create", "--daemon", "-num_instances=2"},
		},
		{
			name:           "missing",
			subCommandArgs: []string{"stop"},
			flagName:       "wait_for_launcher",
			value:          "10",
			want:           []string{"stop", "-wait_for_launcher=10"},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			cvdArgs := &CvdArgs{SubCommandArgs: tc.subCommandArgs}
			cvdArgs.ReplaceFlagValueOnSubCommandArgs(tc.flagName, tc.value)
			if !slices.Equal(cvdArgs.SubCommandArgs, tc.want) {
				t.Errorf("SubCommandArgs after ReplaceFlagValueOnSubCommandArgs(%q, %q) = %q; want %q", tc.flagName, tc.value, cvdArgs.SubCommandArgs, tc.want)
			}
		})
	}
}
