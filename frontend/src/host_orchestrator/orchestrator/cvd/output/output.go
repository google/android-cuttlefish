// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package output

// The types in this file are used to parse the JSON output of cvd commands.

// A CVD instance group
type Group struct {
	Name      string      `json:"group_name"`
	Instances []*Instance `json:"instances"`
}

// A CVD instance
type Instance struct {
	InstanceName   string   `json:"instance_name"`
	Status         string   `json:"status"`
	Displays       []string `json:"displays"`
	InstanceDir    string   `json:"instance_dir"`
	WebRTCDeviceID string   `json:"webrtc_device_id"`
	ADBSerial      string   `json:"adb_serial"`
}

// The output of the cvd fleet command
type Fleet struct {
	Groups []*Group `json:"groups"`
}

type DisplayMode struct {
	Windowed []int `json:"windowed"`
}

type Display struct {
	DPI           []int       `json:"dpi"`
	Mode          DisplayMode `json:"mode"`
	RefreshRateHZ int         `json:"refresh-rate"`
}

// The output of the `cvd displays` command
type Displays struct {
	Displays map[int]*Display `json:"displays"`
}

// The output of the `cvd screen_recordings list` command
type ScreenRecordingsList []struct {
	GroupName    string   `json:"group_name"`
	InstanceName string   `json:"instance_name"`
	Recordings   []string `json:"recordings"`
}
