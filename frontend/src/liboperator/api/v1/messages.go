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

package v1

type RegisterMsg struct {
	Type     string      `json:"message_type"`
	DeviceId string      `json:"device_id"`
	Port     int         `json:"device_port"`
	Info     interface{} `json:"device_info"`
}

type ConnectMsg struct {
	Type     string `json:"message_type"`
	DeviceId string `json:"device_id"`
}

type ForwardMsg struct {
	Type    string      `json:"message_type"`
	Payload interface{} `json:"payload"`
	// This is used by the device message and ignored by the client
	ClientId int `json:"client_id"`
}

type ClientMsg struct {
	Type     string      `json:"message_type"`
	ClientId int         `json:"client_id"`
	Payload  interface{} `json:"payload"`
}

type ErrorMsg struct {
	Error   string `json:"error"`
	Details string `json:"details,omitempty"`
}

type NewConnMsg struct {
	DeviceId string `json:"device_id"`
}

type NewConnReply struct {
	ConnId     string      `json:"connection_id"`
	DeviceInfo interface{} `json:"device_info"`
}

type InfraConfig struct {
	Type       string      `json:"message_type"`
	IceServers []IceServer `json:"ice_servers"`
}
type IceServer struct {
	URLs []string `json:"urls"`
}

type CreateCVDRequest struct {
	// REQUIRED.
	CVD *CVD `json:"cvd"`
}

// Represents a build from ci.android.com.
type AndroidCIBuild struct {
	// The branch name. If omitted the passed `BuildID` will determine the branch.
	Branch string `json:"branch"`
	// Uniquely identifies a branch's snapshot. If empty, the latest green snapshot of the used branch will
	// be used.
	BuildID string `json:"build_id"`
	// A string to determine the specific product and flavor from the set of builds.
	Target string `json:"target"`
}

type AndroidCIBuildSource struct {
	// Main build. If omitted, defaults to branch "aosp-master" and target `aosp_cf_x86_64_phone-userdebug`.
	MainBuild *AndroidCIBuild `json:"main_build,omitempty"`
	// Uses this specific kernel build target if set.
	KernelBuild *AndroidCIBuild `json:"kernel_build,omitempty"`
	// Uses this specific bootloader build target if set.
	BootloaderBuild *AndroidCIBuild `json:"bootloader_build,omitempty"`
	// Uses this specific system image build target if set.
	SystemImageBuild *AndroidCIBuild `json:"system_image_build,omitempty"`
}

// Represents a user build.
type UserBuildSource struct {
	// [REQUIRED] Name of the directory where the user artifacts are stored.
	ArtifactsDir string `json:"artifacts_dir"`
}

// Represents the artifacts source to build the CVD.
type BuildSource struct {
	// A build from ci.android.com
	AndroidCIBuildSource *AndroidCIBuildSource `json:"android_ci_build_source,omitempty"`
	// A user build.
	UserBuildSource *UserBuildSource `json:"user_build_source,omitempty"`
}

type Operation struct {
	Name string `json:"name"`
	// If the value is `false`, it means the operation is still in progress.
	// If `true`, the operation is completed, and either `error` or `response` is
	// available.
	Done bool `json:"done"`
}

type CVD struct {
	// [Output Only]
	Name string `json:"name"`
	// [REQUIRED]
	BuildSource *BuildSource `json:"build_source"`
	// [Output Only]
	Status string `json:"status"`
	// [Output Only]
	Displays []string `json:"displays"`
}

type DeviceInfoReply struct {
	DeviceId         string      `json:"device_id"`
	RegistrationInfo interface{} `json:"registration_info"`
}

type ListCVDsResponse struct {
	CVDs []*CVD `json:"cvds"`
}

type UploadDirectory struct {
	// [Output Only]
	Name string `json:"name"`
}

type ListUploadDirectoriesResponse struct {
	Items []*UploadDirectory `json:"items"`
}
