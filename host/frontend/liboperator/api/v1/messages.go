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
	Error string `json:"error"`
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
	BuildInfo *BuildInfo `json:"build_info"`
	// The number of CVDs to create. Use this field if creating more than one instance.
	// Defaults to 1.
	InstancesCount int `json:"instances_count"`
	// REQUIRED. The build id used to download the fetch_cvd binary from.
	FetchCVDBuildID string `json:"fetch_cvd_build_id"`
}

type BuildInfo struct {
	// [REQUIRED] The Android build identifier.
	BuildID string `json:"build_id"`
	// [REQUIRED] A string to determine the specific product and flavor from
	// the set of builds, e.g. aosp_cf_x86_64_phone-userdebug.
	Target string `json:"target"`
}

type Operation struct {
	Name string `json:"name"`
	// If the value is `false`, it means the operation is still in progress.
	// If `true`, the operation is completed, and either `error` or `response` is
	// available.
	Done bool `json:"done"`
	// Result will contain either an error or a result object but never both.
	Result *OperationResult `json:"result,omitempty"`
}

type OperationResult struct {
	Error *ErrorMsg `json:"error,omitempty"`
}
