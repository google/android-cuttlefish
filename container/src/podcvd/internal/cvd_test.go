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
	"reflect"
	"strings"
	"testing"
)

func TestParseInstanceGroup(t *testing.T) {
	tests := []struct {
		name      string
		jsonStr   string
		groupName string
		want      *InstanceGroup
		wantErr   bool
	}{
		{
			name:      "valid",
			jsonStr:   `{"group_name":"cvd_1","instances":[{"adb_port":6520}]}`,
			groupName: "cvd_1",
			want:      &InstanceGroup{GroupName: "cvd_1", Instances: []Instance{{AdbPort: 6520}}},
		},
		{
			name:      "group name mismatch",
			jsonStr:   `{"group_name":"cvd_2","instances":[{"adb_port":6521}]}`,
			groupName: "cvd_1",
			wantErr:   true,
		},
		{
			name:      "missing group name",
			jsonStr:   `{"instances":[{"adb_port":6522}]}`,
			groupName: "",
			wantErr:   true,
		},
		{
			name:      "invalid json",
			jsonStr:   `{"group_name":`,
			groupName: "cvd_3",
			wantErr:   true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ParseInstanceGroup(tc.jsonStr, tc.groupName)
			if tc.wantErr {
				if err == nil {
					t.Errorf("ParseInstanceGroup(%q, %q) succeeded; want error", tc.jsonStr, tc.groupName)
				}
				return
			}
			if err != nil {
				t.Fatalf("ParseInstanceGroup(%q, %q) failed: %v", tc.jsonStr, tc.groupName, err)
			}
			if !reflect.DeepEqual(got, tc.want) {
				t.Errorf("ParseInstanceGroup(%q, %q) = %+v; want %+v", tc.jsonStr, tc.groupName, got, tc.want)
			}
		})
	}
}

func TestParseInstanceGroups(t *testing.T) {
	tests := []struct {
		name      string
		jsonStr   string
		groupName string
		want      *InstanceGroup
		wantErr   bool
	}{
		{
			name:      "single group",
			jsonStr:   `{"groups":[{"group_name":"cvd_4","instances":[{"adb_port":6523},{"adb_port":6524}]}]}`,
			groupName: "cvd_4",
			want:      &InstanceGroup{GroupName: "cvd_4", Instances: []Instance{{AdbPort: 6523}, {AdbPort: 6524}}},
		},
		{
			name:      "multiple groups",
			jsonStr:   `{"groups":[{"group_name":"cvd_5","instances":[]},{"group_name":"cvd_6","instances":[]}]}`,
			groupName: "cvd_5",
			wantErr:   true,
		},
		{
			name:      "group name mismatch",
			jsonStr:   `{"groups":[{"group_name":"cvd_7","instances":[]}]}`,
			groupName: "cvd_8",
			wantErr:   true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ParseInstanceGroups(tc.jsonStr, tc.groupName)
			if tc.wantErr {
				if err == nil {
					t.Errorf("ParseInstanceGroups(%q, %q) succeeded; want error", tc.jsonStr, tc.groupName)
				}
				return
			}
			if err != nil {
				t.Fatalf("ParseInstanceGroups(%q, %q) failed: %v", tc.jsonStr, tc.groupName, err)
			}
			if !reflect.DeepEqual(got, tc.want) {
				t.Errorf("ParseInstanceGroups(%q, %q) = %+v; want %+v", tc.jsonStr, tc.groupName, got, tc.want)
			}
		})
	}
}

func TestUpdateCvdGroupJsonRaw(t *testing.T) {
	tests := []struct {
		name          string
		data          any
		podcvdBaseDir string
		ipAddr        string
		want          any
	}{
		{
			name:          "path in map",
			data:          map[string]any{"instance_dir": "/var/tmp/cvd/1000/1/instances/cvd-1"},
			podcvdBaseDir: "/home/user/.podcvd/cvd_1",
			ipAddr:        "192.168.80.2",
			want:          map[string]any{"instance_dir": "/home/user/.podcvd/cvd_1/instances/cvd-1"},
		},
		{
			name:          "path in array",
			data:          []any{"/var/tmp/cvd/1000/2/assembly"},
			podcvdBaseDir: "/home/user/.podcvd/cvd_2",
			ipAddr:        "192.168.80.3",
			want:          []any{"/home/user/.podcvd/cvd_2/assembly"},
		},
		{
			name:          "nested path",
			data:          map[string]any{"instances": []any{map[string]any{"instance_dir": "/var/tmp/cvd/1000/3/instances/cvd-3"}}},
			podcvdBaseDir: "/home/user/.podcvd/cvd_3",
			ipAddr:        "192.168.80.4",
			want:          map[string]any{"instances": []any{map[string]any{"instance_dir": "/home/user/.podcvd/cvd_3/instances/cvd-3"}}},
		},
		{
			name:          "address",
			data:          map[string]any{"webrtc_device_id": "localhost:8443"},
			podcvdBaseDir: "/home/user/.podcvd/cvd_4",
			ipAddr:        "192.168.80.5",
			want:          map[string]any{"webrtc_device_id": "192.168.80.5:8443"},
		},
		{
			name:          "non-string value",
			data:          map[string]any{"adb_port": float64(6520)},
			podcvdBaseDir: "/home/user/.podcvd/cvd_5",
			ipAddr:        "192.168.80.6",
			want:          map[string]any{"adb_port": float64(6520)},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			UpdateCvdGroupJsonRaw(tc.data, tc.podcvdBaseDir, tc.ipAddr)
			if !reflect.DeepEqual(tc.data, tc.want) {
				t.Errorf("data after UpdateCvdGroupJsonRaw(%q, %q) = %v; want %v", tc.podcvdBaseDir, tc.ipAddr, tc.data, tc.want)
			}
		})
	}
}

func TestPrintPsOutputs(t *testing.T) {
	tests := []struct {
		name    string
		results []HostExecResult
		want    string
		wantErr bool
	}{
		{
			name: "single host",
			results: []HostExecResult{
				{GroupName: "cvd_1", IP: "192.168.80.2", Stdout: []byte("GROUP   STATUS\ncvd_1   Running\n")},
			},
			want: "GROUP   STATUS\ncvd_1   Running\n",
		},
		{
			name: "multiple hosts",
			results: []HostExecResult{
				{GroupName: "cvd_1", IP: "192.168.80.2", Stdout: []byte("GROUP   STATUS\ncvd_1   Running\n")},
				{GroupName: "cvd_2", IP: "192.168.80.3", Stdout: []byte("GROUP   STATUS\ncvd_2   Stopped\n")},
			},
			want: "GROUP   STATUS\ncvd_1   Running\ncvd_2   Stopped\n",
		},
		{
			name: "different columns",
			results: []HostExecResult{
				{GroupName: "cvd_3", IP: "192.168.80.4", Stdout: []byte("GROUP   STATUS\ncvd_3   Running\n")},
				{GroupName: "cvd_4", IP: "192.168.80.5", Stdout: []byte("GROUP   NAME   STATUS\ncvd_4   1      Running\n")},
			},
			want: "GROUP   STATUS    NAME\ncvd_3   Running   \ncvd_4   Running   1\n",
		},
		{
			name: "empty output",
			results: []HostExecResult{
				{GroupName: "cvd_5", IP: "192.168.80.6", Stdout: []byte("")},
			},
			wantErr: true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			var out strings.Builder
			err := PrintPsOutputs(tc.results, &out)
			if tc.wantErr {
				if err == nil {
					t.Errorf("PrintPsOutputs() succeeded; want error")
				}
				return
			}
			if err != nil {
				t.Fatalf("PrintPsOutputs() failed: %v", err)
			}
			if got := out.String(); got != tc.want {
				t.Errorf("PrintPsOutputs() output = %q; want %q", got, tc.want)
			}
		})
	}
}

func TestUpdateIPAndPortString(t *testing.T) {
	tests := []struct {
		name   string
		data   string
		ipAddr string
		want   string
	}{
		{
			name:   "operator address",
			data:   "https://0.0.0.0:1443/devices",
			ipAddr: "192.168.80.2",
			want:   "https://192.168.80.2:11443/devices",
		},
		{
			name:   "localhost",
			data:   "localhost:8443",
			ipAddr: "192.168.80.3",
			want:   "192.168.80.3:8443",
		},
		{
			name:   "loopback address",
			data:   "127.0.0.1:6520",
			ipAddr: "192.168.80.4",
			want:   "192.168.80.4:6520",
		},
		{
			name:   "no address",
			data:   "cvd_1 Running",
			ipAddr: "192.168.80.5",
			want:   "cvd_1 Running",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if got := updateIPAndPortString(tc.data, tc.ipAddr); got != tc.want {
				t.Errorf("updateIPAndPortString(%q, %q) = %q; want %q", tc.data, tc.ipAddr, got, tc.want)
			}
		})
	}
}
