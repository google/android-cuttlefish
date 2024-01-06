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

// Control messages

type ControlMsgHeader struct {
	Type string `json:"message_type"`
}

type PreRegisterMsg struct {
	// Type must be set to "pre-register"
	ControlMsgHeader
	GroupName string `json:"group_name"`
	Owner     string `json:"owner"`
	Devices   []struct {
		Id   string `json:"id"`
		Name string `json:"name"`
	} `json:"devices"`
}

type RegistrationStatusReport struct {
	Id     string `json:"id"`
	Status string `json:"status"`
	Msg    string `json:"message"`
}

type PreRegistrationResponse []RegistrationStatusReport

// Device and client messages

type RegisterMsg struct {
	DeviceId string      `json:"device_id"`
	Port     int         `json:"device_port"`
	Info     interface{} `json:"device_info"`
}

type ConnectMsg struct {
	DeviceId string `json:"device_id"`
}

type ForwardMsg struct {
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

type FetchArtifactsRequest struct {
	AndroidCIBundle *AndroidCIBundle `json:"android_ci_bundle"`
}

type FetchArtifactsResponse struct {
	AndroidCIBundle *AndroidCIBundle `json:"android_ci_bundle"`
}

type ArtifactsBundleType int64

const (
	MainBundleType ArtifactsBundleType = iota
	KernelBundleType
	BootloaderBundleType
	SystemImageBundleType
)

type AndroidCIBundle struct {
	// If omitted, defaults to branch "aosp-main" and target `aosp_cf_x86_64_phone-trunk_staging-userdebug`.
	Build *AndroidCIBuild `json:"build,omitempty"`
	// If omitted, it defaults to the `main` bundle type.
	Type ArtifactsBundleType `json:"type"`
}

// Use `X-Cutf-Host-Orchestrator-BuildAPI-Creds` http header to pass the Build API credentials.
type CreateCVDRequest struct {
	// Environment canonical configuration.
	// Structure: https://android.googlesource.com/device/google/cuttlefish/+/8bbd3b9cd815f756f332791d45c4f492b663e493/host/commands/cvd/parser/README.md
	// Example: https://cs.android.com/android/platform/superproject/main/+/main:device/google/cuttlefish/host/cvd_test_configs/main_phone-main_watch.json;drc=b2e8f4f014abb7f9cb56c0ae199334aacb04542d
	// NOTE: Using this as a black box for now as its content is unstable. Use the test configs pointed
	// above as reference to build your config object.
	EnvConfig map[string]interface{} `json:"env_config"`

	// [DEPRECATED]. Use `EnvConfig` field.
	CVD *CVD `json:"cvd"`
	// [DEPRECATED]. Use `EnvConfig` field.
	// Use to create multiple homogeneous instances.
	AdditionalInstancesNum uint32 `json:"additional_instances_num,omitempty"`
}

type CreateCVDResponse struct {
	CVDs []*CVD `json:"cvds"`
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
	// Main build. If omitted, defaults to branch "aosp-main" and target `aosp_cf_x86_64_phone-trunk_staging-userdebug`.
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
	// [Output Only] The group name the instance belongs to.
	Group string `json:"group"`
	// [Output Only] Identifier within a group.
	Name string `json:"name"`
	// [REQUIRED]
	BuildSource *BuildSource `json:"build_source"`
	// [Output Only]
	Status string `json:"status"`
	// [Output Only]
	Displays []string `json:"displays"`
	// [Output Only]
	WebRTCDeviceID string `json:"webrtc_device_id"`
}

// Identifier within the whole fleet. Format: "{group}/{name}".
func (c *CVD) ID() string { return c.Group + "/" + c.Name }

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

type StopCVDResponse struct{}
