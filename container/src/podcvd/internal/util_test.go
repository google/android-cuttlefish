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
	"maps"
	"slices"
	"testing"
)

type containerLister struct {
	CuttlefishContainerManager
	running    []ContainerListEntry
	nonRunning []ContainerListEntry
}

func (c containerLister) ListContainers(_ context.Context, all bool) ([]ContainerListEntry, error) {
	if all {
		return append(slices.Clone(c.running), c.nonRunning...), nil
	}
	return c.running, nil
}

func TestIpv4AddressesByGroupNames(t *testing.T) {
	tests := []struct {
		name            string
		clientID        string
		nonRunningCtrs  bool
		otherClientCtrs bool
		running         []ContainerListEntry
		nonRunning      []ContainerListEntry
		want            map[string]string
		wantErr         bool
	}{
		{
			name:            "operator port address",
			clientID:        "",
			nonRunningCtrs:  false,
			otherClientCtrs: false,
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
					Ports:  []PortBinding{{IP: "192.168.80.2", PrivatePort: 1443}},
				},
			},
			want: map[string]string{"cvd_1": "192.168.80.1", "cvd_2": "192.168.80.2"},
		},
		{
			name:            "running containers only",
			clientID:        "",
			nonRunningCtrs:  false,
			otherClientCtrs: false,
			running: []ContainerListEntry{
				{
					Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
					Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
				},
			},
			nonRunning: []ContainerListEntry{
				{
					Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_2"},
					Ports:  []PortBinding{{IP: "192.168.80.2", PrivatePort: 1443}},
				},
			},
			want: map[string]string{"cvd_1": "192.168.80.1"},
		},
		{
			name:            "non-running containers included",
			clientID:        "",
			nonRunningCtrs:  true,
			otherClientCtrs: false,
			running: []ContainerListEntry{
				{
					Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
					Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
				},
			},
			nonRunning: []ContainerListEntry{
				{
					Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_2"},
					Ports:  []PortBinding{{IP: "192.168.80.2", PrivatePort: 1443}},
				},
			},
			want: map[string]string{"cvd_1": "192.168.80.1", "cvd_2": "192.168.80.2"},
		},
		{
			name:            "container without group name",
			clientID:        "",
			nonRunningCtrs:  false,
			otherClientCtrs: false,
			running: []ContainerListEntry{
				{
					Labels: map[string]string{"created_by": "podcvd"},
					Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
				},
			},
			want: map[string]string{},
		},
		{
			name:            "other client container",
			clientID:        "client_a",
			nonRunningCtrs:  false,
			otherClientCtrs: false,
			running: []ContainerListEntry{
				{
					Labels: map[string]string{"group_name": "cvd_1", "client_id": "client_b"},
					Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
				},
			},
			want: map[string]string{},
		},
		{
			name:            "other client container included",
			clientID:        "client_a",
			nonRunningCtrs:  false,
			otherClientCtrs: true,
			running: []ContainerListEntry{
				{
					Labels: map[string]string{"group_name": "cvd_1", "client_id": "client_b"},
					Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
				},
			},
			want: map[string]string{"cvd_1": "192.168.80.1"},
		},
		{
			name:            "missing operator port",
			clientID:        "",
			nonRunningCtrs:  false,
			otherClientCtrs: false,
			running: []ContainerListEntry{
				{
					Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
					Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 6520}},
				},
			},
			wantErr: true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			t.Setenv(envClientID, tc.clientID)
			ccm := containerLister{running: tc.running, nonRunning: tc.nonRunning}
			got, err := Ipv4AddressesByGroupNames(ccm, tc.nonRunningCtrs, tc.otherClientCtrs)
			if tc.wantErr {
				if err == nil {
					t.Errorf("Ipv4AddressesByGroupNames(%v) succeeded; want error",
						tc.otherClientCtrs)
				}
				return
			}
			if err != nil {
				t.Fatalf("Ipv4AddressesByGroupNames(%v) failed: %v", tc.otherClientCtrs, err)
			}
			if !maps.Equal(got, tc.want) {
				t.Errorf("Ipv4AddressesByGroupNames(%v) = %v; want %v",
					tc.otherClientCtrs, got, tc.want)
			}
		})
	}
}

func TestContainerName(t *testing.T) {
	tests := []struct {
		name      string
		groupName string
		want      string
	}{
		{
			name:      "first group",
			groupName: "cvd_1",
			want:      "d10f7d5f996b",
		},
		{
			name:      "second group",
			groupName: "cvd_2",
			want:      "7dafbed2fa09",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if got := ContainerName(tc.groupName); got != tc.want {
				t.Errorf("ContainerName(%q) = %q; want %q", tc.groupName, got, tc.want)
			}
		})
	}
}
