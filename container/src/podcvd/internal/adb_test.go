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
	"path/filepath"
	"testing"
)

func setupFakeAdb(t *testing.T, script string) {
	t.Helper()
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "adb"), []byte(script), 0755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", dir)
}

func TestConnectAdb(t *testing.T) {
	setupFakeAdb(t, `#!/bin/sh
case "$*" in
"start-server") exit 0 ;;
"connect 192.168.80.1:6520") exit 0 ;;
"connect 192.168.80.1:6521") exit 0 ;;
*) exit 1 ;;
esac
`)
	t.Setenv(envClientID, "")
	ccm := containerLister{
		running: []ContainerListEntry{
			{
				Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
				Ports: []PortBinding{
					{IP: "192.168.80.1", PrivatePort: 6520},
					{IP: "192.168.80.1", PrivatePort: 1443},
				},
			},
			{
				Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_2"},
				Ports: []PortBinding{
					{IP: "192.168.80.2", PrivatePort: 6520},
					{IP: "192.168.80.2", PrivatePort: 1443},
				},
			},
		},
	}

	tests := []struct {
		name          string
		instanceGroup InstanceGroup
		wantErr       bool
	}{
		{
			name: "success",
			instanceGroup: InstanceGroup{
				GroupName: "cvd_1",
				Instances: []Instance{{AdbPort: 6520}, {AdbPort: 6521}},
			},
		},
		{
			name: "group not running",
			instanceGroup: InstanceGroup{
				GroupName: "cvd_3",
				Instances: []Instance{{AdbPort: 6520}},
			},
			wantErr: true,
		},
		{
			name: "adb failure",
			instanceGroup: InstanceGroup{
				GroupName: "cvd_2",
				Instances: []Instance{{AdbPort: 6520}},
			},
			wantErr: true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := ConnectAdb(ccm, tc.instanceGroup)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("ConnectAdb(%+v) error = %v; want error %v",
					tc.instanceGroup, err, tc.wantErr)
			}
		})
	}
}

func TestConnectAdbWithoutAdb(t *testing.T) {
	t.Setenv("PATH", t.TempDir())
	t.Setenv(envClientID, "")
	ccm := containerLister{
		running: []ContainerListEntry{
			{
				Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
				Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
			},
		},
	}
	group := InstanceGroup{GroupName: "cvd_1", Instances: []Instance{{AdbPort: 6520}}}

	if err := ConnectAdb(ccm, group); err == nil {
		t.Errorf("ConnectAdb(%+v) succeeded without adb; want error", group)
	}
}

func TestDisconnectAdb(t *testing.T) {
	setupFakeAdb(t, `#!/bin/sh
case "$*" in
"start-server") exit 0 ;;
"disconnect 192.168.80.1:6520") exit 0 ;;
"disconnect 192.168.80.1:6521") exit 0 ;;
*) exit 1 ;;
esac
`)
	t.Setenv(envClientID, "")
	ccm := containerLister{
		running: []ContainerListEntry{
			{
				Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
				Ports: []PortBinding{
					{IP: "192.168.80.1", PrivatePort: 6520},
					{IP: "192.168.80.1", PrivatePort: 1443},
				},
			},
			{
				Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_2"},
				Ports: []PortBinding{
					{IP: "192.168.80.2", PrivatePort: 6520},
					{IP: "192.168.80.2", PrivatePort: 1443},
				},
			},
		},
	}

	tests := []struct {
		name          string
		instanceGroup InstanceGroup
		wantErr       bool
	}{
		{
			name: "success",
			instanceGroup: InstanceGroup{
				GroupName: "cvd_1",
				Instances: []Instance{{AdbPort: 6520}, {AdbPort: 6521}},
			},
		},
		{
			name: "group not running",
			instanceGroup: InstanceGroup{
				GroupName: "cvd_3",
				Instances: []Instance{{AdbPort: 6520}},
			},
			wantErr: true,
		},
		{
			name: "adb failure",
			instanceGroup: InstanceGroup{
				GroupName: "cvd_2",
				Instances: []Instance{{AdbPort: 6520}},
			},
			wantErr: true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := DisconnectAdb(ccm, tc.instanceGroup)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("DisconnectAdb(%+v) error = %v; want error %v",
					tc.instanceGroup, err, tc.wantErr)
			}
		})
	}
}

func TestDisconnectAdbWithoutAdb(t *testing.T) {
	t.Setenv("PATH", t.TempDir())
	t.Setenv(envClientID, "")
	ccm := containerLister{
		running: []ContainerListEntry{
			{
				Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
				Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
			},
		},
	}
	group := InstanceGroup{GroupName: "cvd_1", Instances: []Instance{{AdbPort: 6520}}}

	if err := DisconnectAdb(ccm, group); err == nil {
		t.Errorf("DisconnectAdb(%+v) succeeded without adb; want error", group)
	}
}
