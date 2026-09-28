// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package e2etests

import (
	"testing"
)

func TestParseCVDStatusJSON(t *testing.T) {
	tests := []struct {
		name      string
		input     string
		wantCount int
		wantName  string
		wantStat  string
		wantErr   bool
	}{
		{
			name: "Array format",
			input: `[
				{
					"adb_port": 6520,
					"adb_serial": "127.0.0.1:6520",
					"instance_name": "ins-1",
					"status": "Running"
				}
			]`,
			wantCount: 1,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name: "Group object format",
			input: `{
				"instances": [
					{
						"adb_port": 6520,
						"adb_serial": "127.0.0.1:6520",
						"instance_name": "ins-1",
						"status": "Running"
					},
					{
						"adb_port": 6521,
						"adb_serial": "127.0.0.1:6521",
						"instance_name": "ins-2",
						"status": "Running"
					}
				]
			}`,
			wantCount: 2,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name: "Log prefix with group object",
			input: `[INFO 2026-09-10 12:34:56] cvd status output:
			{
				"instances": [
					{
						"adb_port": 6520,
						"adb_serial": "127.0.0.1:6520",
						"instance_name": "ins-1",
						"status": "Running"
					}
				]
			}`,
			wantCount: 1,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name: "Log prefix with array",
			input: `[INFO 2026-09-10 12:34:56] cvd status output:
			[
				{
					"adb_port": 6520,
					"adb_serial": "127.0.0.1:6520",
					"instance_name": "ins-1",
					"status": "Running"
				}
			]`,
			wantCount: 1,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name: "Single object format",
			input: `{
				"adb_port": 6520,
				"adb_serial": "127.0.0.1:6520",
				"instance_name": "ins-1",
				"status": "Running"
			}`,
			wantCount: 1,
			wantName:  "ins-1",
			wantStat:  "Running",
		},
		{
			name:    "Invalid output",
			input:   "error: cvd server is not running",
			wantErr: true,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			entries, err := ParseCVDStatusJSON(tc.input)
			if tc.wantErr {
				if err == nil {
					t.Fatalf("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if len(entries) != tc.wantCount {
				t.Fatalf("expected %d entries, got %d", tc.wantCount, len(entries))
			}
			if entries[0].InstanceName != tc.wantName {
				t.Errorf("expected instance name %q, got %q", tc.wantName, entries[0].InstanceName)
			}
			if entries[0].Status != tc.wantStat {
				t.Errorf("expected status %q, got %q", tc.wantStat, entries[0].Status)
			}
		})
	}
}
