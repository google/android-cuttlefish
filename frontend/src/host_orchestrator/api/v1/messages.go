// Copyright 2024 Google LLC
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
	// If omitted, defaults to branch "aosp-android-latest-release" and target `aosp_cf_x86_64_only_phone-userdebug`.
	Build *AndroidCIBuild `json:"build,omitempty"`
	// If omitted, it defaults to the `main` bundle type.
	Type ArtifactsBundleType `json:"type"`
}

// Prefix for specifying image directory ID while creating CVD with CreateCVDRequest.EnvConfig.
const EnvConfigImageDirectoriesVar = "@image_dirs"

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
	// A string to determine the specific device product and flavor
	Target string `json:"target"`
}

type AndroidCIBuildSource struct {
	// Main build. If omitted, defaults to branch "aosp-android-latest-release" and target `aosp_cf_x86_64_only_phone-userdebug`.
	MainBuild *AndroidCIBuild `json:"main_build,omitempty"`
	// Uses this specific kernel build target if set.
	KernelBuild *AndroidCIBuild `json:"kernel_build,omitempty"`
	// Uses this specific bootloader build target if set.
	BootloaderBuild *AndroidCIBuild `json:"bootloader_build,omitempty"`
	// Uses this specific system image build target if set.
	SystemImageBuild *AndroidCIBuild `json:"system_image_build,omitempty"`
}

// Represents the artifacts source to build the CVD.
type BuildSource struct {
	// A build from ci.android.com
	AndroidCIBuildSource *AndroidCIBuildSource `json:"android_ci_build_source,omitempty"`
}

type ListOperationsResponse struct {
	Operations []Operation `json:"operations"`
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
	// [Input Only]
	BuildSource *BuildSource `json:"build_source,omitempty"`
	// [Output Only]
	Status string `json:"status"`
	// [Output Only]
	Displays []string `json:"displays"`
	// [Output Only]
	WebRTCDeviceID string `json:"webrtc_device_id"`
	// [Output Only]
	ADBSerial string `json:"adb_serial"`
}

// Identifier within the whole fleet. Format: "{group}/{name}".
func (c *CVD) ID() string { return c.Group + "/" + c.Name }

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

type StatArtifactResponse struct{}

type ImageDirectory struct {
	ID string `json:"id"`
}

type CreateImageDirectoryResponse struct {
	// [Output Only] Identifier of created image directory.
	ID string `json:"id"`
}

type ListImageDirectoriesResponse struct {
	// [Output Only] Identifiers of image directories.
	ImageDirs []ImageDirectory `json:"image_dirs"`
}

type UpdateImageDirectoryRequest struct {
	UserArtifactChecksum string `json:"user_artifact_checksum"`
}

type CreateSnapshotRequest struct {
	// [Optional]
	// Value must match regex: "^([a-z0-9\\-]+)$".
	SnapshotID string `json:"snapshot_id,omitempty"`
}

type CreateSnapshotResponse struct {
	SnapshotID string `json:"snapshot_id"`
}

type EmptyResponse struct{}

type StopCVDResponse = EmptyResponse

type StartCVDRequest struct {
	// Start from the relevant snaphost if not empty.
	SnapshotID string `json:"snapshot_id,omitempty"`
}

type DisplayAddRequest struct {
	Width         int `json:"width"`
	Height        int `json:"height"`
	DPI           int `json:"dpi,omitempty"`
	RefreshRateHZ int `json:"refresh_rate_hz,omitempty"`
}

type DisplayAddResponse = struct {
	DisplayNumber int `json:"display_number"`
}

type DisplayMode struct {
	Windowed []int `json:"windowed"`
}

type Display struct {
	DPI           []int       `json:"dpi"`
	Mode          DisplayMode `json:"mode"`
	RefreshRateHZ int         `json:"refresh_rate_hz"`
}

type DisplayListResponse struct {
	Displays map[int]Display `json:"displays"`
}

type DisplayRemoveResponse = EmptyResponse

type DisplayScreenshotRequest struct {
	DisplayNumber int `json:"display_number,omitempty"`
}

type DisplayScreenshotResponse struct {
	ScreenshotBytesBase64 string `json:"screenshot_bytes"`
	ScreenshotMimeType    string `json:"screenshot_mime_type"`
}

type ListScreenRecordingsResponse struct {
	ScreenRecordings []string `json:"screen_recordings"`
}
