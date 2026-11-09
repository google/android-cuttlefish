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
	"fmt"
	"testing"
)

type hostManager struct {
	CuttlefishContainerManager
	containers map[string]ContainerListEntry
}

func (h hostManager) StopAndRemoveContainer(_ context.Context, ctr string) error {
	if _, ok := h.containers[ctr]; !ok {
		return fmt.Errorf("no such container %q", ctr)
	}
	return nil
}

func TestDeleteCuttlefishHost(t *testing.T) {
	tests := []struct {
		name      string
		ccm       hostManager
		groupName string
		wantErr   bool
	}{
		{
			name: "success",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{ContainerName("cvd_1"): {}},
			},
			groupName: "cvd_1",
		},
		{
			name: "failure",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{ContainerName("cvd_1"): {}},
			},
			groupName: "cvd_2",
			wantErr:   true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := DeleteCuttlefishHost(tc.ccm, tc.groupName)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("DeleteCuttlefishHost(%+v, %q) error = %v; want error %v",
					tc.ccm, tc.groupName, err, tc.wantErr)
			}
		})
	}
}
