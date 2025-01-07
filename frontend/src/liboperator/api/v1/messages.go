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
		Id      string `json:"id"`
		Name    string `json:"name"`
		ADBPort int    `json:"adb_port"`
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

type DeviceDescriptor struct {
	DeviceId  string `json:"device_id"`
	GroupName string `json:"group_name"`
	Owner     string `json:"owner,omitempty"`
	Name      string `json:"name,omitempty"`
	ADBPort   int    `json:"adb_port"`
}

type DeviceInfoReply struct {
	DeviceDescriptor
	RegistrationInfo interface{} `json:"registration_info"`
}

