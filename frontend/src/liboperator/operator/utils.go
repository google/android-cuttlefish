// Copyright 2021 Google LLC
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

package operator

import (
	"encoding/json"
	"errors"
	"log"
	"net/http"

	"google.golang.org/grpc"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
)

// Interface implemented by any connection capable of sending in JSON format
type JSONConn interface {
	Send(val interface{}) error
}

// Log and reply with an error
func ReplyError(c JSONConn, msg string) {
	log.Println(msg)
	if err := c.Send(apiv1.ErrorMsg{Error: msg}); err != nil {
		log.Println("Failed to send error reply: ", err)
	}
}

// Send a JSON http response with success status code to the client
func ReplyJSONOK(w http.ResponseWriter, obj interface{}) error {
	return ReplyJSON(w, obj, http.StatusOK)
}

// Send a JSON http response with error to the client
func ReplyJSONErr(w http.ResponseWriter, err error) error {
	log.Printf("response with error: %v\n", err)
	var e *AppError
	if errors.As(err, &e) {
		return ReplyJSON(w, e.JSONResponse(), e.StatusCode)
	}
	return ReplyJSON(w, apiv1.ErrorMsg{Error: "Internal Server Error"}, http.StatusInternalServerError)
}

// Send a JSON http response to the client
func ReplyJSON(w http.ResponseWriter, obj interface{}, statusCode int) error {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	encoder := json.NewEncoder(w)
	return encoder.Encode(obj)
}

// Connect ControlEnvProxyServer
func ConnectControlEnvProxyServer(devId string, pool *DevicePool) (*grpc.ClientConn, error) {
	dev := pool.GetDevice(devId)
	if dev == nil {
		return nil, errors.New("Device not found")
	}

	devInfo := dev.privateData.(map[string]interface{})
	serverPath, ok := devInfo["control_env_proxy_server_path"].(string)
	if !ok {
		return nil, errors.New("ControlEnvProxyServer path not found")
	}
	return grpc.Dial("unix://"+serverPath, grpc.WithInsecure())
}
