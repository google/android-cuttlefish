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
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"strconv"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/gorilla/mux"
)

// Sets up a unix socket for devices to connect to and returns a function that listens on the
// socket until an error occurrs.
func SetupDeviceEndpoint(pool *DevicePool, config apiv1.InfraConfig, path string) func() error {
	if err := os.RemoveAll(path); err != nil {
		return func() error { return fmt.Errorf("Failed to clean previous socket: %w", err) }
	}
	addr, err := net.ResolveUnixAddr("unixpacket", path)
	if err != nil {
		// Returns a loop function that will immediately return an error when invoked
		return func() error { return fmt.Errorf("Failed to create unix address from path: %w", err) }
	}
	sock, err := net.ListenUnix("unixpacket", addr)
	if err != nil {
		return func() error { return fmt.Errorf("Failed to create unix socket: %w", err) }
	}
	// Make sure the socket is only accessible by owner and group
	if err := os.Chmod(path, 0770); err != nil {
		// This shouldn't happen since the creation of the socket just succeeded
		log.Println("Failed to change permissions on socket file: ", err)
	}
	log.Println("Device endpoint created")
	// Serve the register_device endpoint in a background thread
	return func() error {
		defer sock.Close()
		for {
			c, err := sock.AcceptUnix()
			if err != nil {
				return err
			}
			go deviceEndpoint(NewJSONUnix(c), pool, config)
		}
	}
}

// Creates a router with handlers for the following endpoints:
// GET  /infra_config
// GET  /devices
// GET  /devices/{deviceId}
// GET  /devices/{deviceId}/files/{path}
// GET  /polled_connections
// GET  /polled_connections/{connId}/messages
// POST /polled_connections/{connId}/:forward
// The maybeIntercept parameter is a function that accepts the
// requested device file and returns a path to a file to be returned instead or
// nil if the request should be allowed to proceed to the device.
func CreateHttpHandlers(
	pool *DevicePool,
	polledSet *PolledSet,
	config apiv1.InfraConfig,
	maybeIntercept func(string) *string) *mux.Router {
	router := mux.NewRouter()
	// The path parameter needs to include the leading '/'
	router.HandleFunc("/devices/{deviceId}/files{path:/.+}", func(w http.ResponseWriter, r *http.Request) {
		deviceFiles(w, r, pool, maybeIntercept)
	}).Methods("GET")
	router.HandleFunc("/devices", func(w http.ResponseWriter, r *http.Request) {
		listDevices(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/devices/{deviceId}", func(w http.ResponseWriter, r *http.Request) {
		deviceInfo(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/polled_connections/{connId}/:forward", func(w http.ResponseWriter, r *http.Request) {
		forward(w, r, polledSet)
	}).Methods("POST")
	router.HandleFunc("/polled_connections/{connId}/messages", func(w http.ResponseWriter, r *http.Request) {
		messages(w, r, polledSet)
	}).Methods("GET")
	router.HandleFunc("/polled_connections", func(w http.ResponseWriter, r *http.Request) {
		createPolledConnection(w, r, pool, polledSet)
	}).Methods("POST")
	router.HandleFunc("/infra_config", func(w http.ResponseWriter, r *http.Request) {
		ReplyJSONOK(w, config)
	}).Methods("GET")
	return router
}

// Device endpoint
func deviceEndpoint(c *JSONUnix, pool *DevicePool, config apiv1.InfraConfig) {
	log.Println("Device connected")
	defer c.Close()
	var msg apiv1.RegisterMsg
	if err := c.Recv(&msg); err != nil {
		log.Println("Error reading from device: ", err)
		return
	}
	if msg.Type != "register" {
		ReplyError(c, "First device message must be the registration")
		return
	}
	id := msg.DeviceId
	if id == "" {
		ReplyError(c, "Missing device_id")
		return
	}
	info := msg.Info
	if info == nil {
		log.Println("No device info provided by: ", id)
		info = make(map[string]interface{})
	}
	port := msg.Port
	device := NewDevice(c, port, info)
	if !pool.Register(device, id) {
		ReplyError(c, fmt.Sprintln("Device id already taken: ", id))
		return
	}
	defer pool.Unregister(id)
	if err := device.Send(config); err != nil {
		log.Println("Failed to send config to device: ", err)
		return
	}
	for {
		var msg apiv1.ForwardMsg
		if err := c.Recv(&msg); err != nil {
			log.Println("Error reading from device: ", err)
			return
		}
		if msg.Type != "forward" {
			ReplyError(c, fmt.Sprintln("Unrecognized message type: ", msg.Type))
			return
		}
		clientId := msg.ClientId
		if clientId == 0 {
			ReplyError(c, "Device forward message missing client id")
			return
		}
		payload := msg.Payload
		if payload == nil {
			ReplyError(c, "Device forward message missing payload")
			return
		}
		dMsg := map[string]interface{}{
			"message_type": "device_msg",
			"payload":      payload,
		}
		if err := device.ToClient(clientId, dMsg); err != nil {
			log.Println("Device: ", id, " failed to send message to client: ", err)
			ReplyError(c, fmt.Sprintln("Client disconnected: ", clientId))
		}
	}
}

// General client endpoints

func listDevices(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	if err := ReplyJSONOK(w, pool.DeviceIds()); err != nil {
		log.Println(err)
	}
}

// Get device info

func deviceInfo(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	vars := mux.Vars(r)
	devId := vars["deviceId"]
	dev := pool.GetDevice(devId)
	if dev == nil {
		http.NotFound(w, r)
		return
	}
	ReplyJSONOK(w, apiv1.DeviceInfoReply{DeviceId: devId, RegistrationInfo: dev.info})
}

func deviceFiles(w http.ResponseWriter, r *http.Request, pool *DevicePool, maybeIntercept func(string) *string) {
	vars := mux.Vars(r)
	devId := vars["deviceId"]
	dev := pool.GetDevice(devId)
	if dev == nil {
		http.NotFound(w, r)
		return
	}
	path := vars["path"]
	if alt := maybeIntercept(path); alt != nil {
		http.ServeFile(w, r, *alt)
	} else {
		r.URL.Path = path
		dev.Proxy.ServeHTTP(w, r)
	}
}

// Http long polling client endpoints

func createPolledConnection(w http.ResponseWriter, r *http.Request, pool *DevicePool, polledSet *PolledSet) {
	var msg apiv1.NewConnMsg
	err := json.NewDecoder(r.Body).Decode(&msg)
	if err != nil {
		log.Println("Failed to parse json from client: ", err)
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	log.Println("id: ", msg.DeviceId)
	device := pool.GetDevice(msg.DeviceId)
	if device == nil {
		http.Error(w, "Device not found", http.StatusNotFound)
		return
	}
	conn := polledSet.NewConnection(device)
	reply := apiv1.NewConnReply{ConnId: conn.Id(), DeviceInfo: device.info}
	ReplyJSONOK(w, reply)
}

func forward(w http.ResponseWriter, r *http.Request, polledSet *PolledSet) {
	id := mux.Vars(r)["connId"]
	conn := polledSet.GetConnection(id)
	if conn == nil {
		http.NotFound(w, r)
		return
	}
	var msg apiv1.ForwardMsg
	err := json.NewDecoder(r.Body).Decode(&msg)
	if err != nil {
		log.Println("Failed to parse json from client: ", err)
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	cMsg := apiv1.ClientMsg{
		Type:     "client_msg",
		ClientId: conn.ClientId(),
		Payload:  msg.Payload,
	}
	if err := conn.ToDevice(cMsg); err != nil {
		log.Println("Failed to send message to device: ", err)
		http.Error(w, "Device disconnected", http.StatusNotFound)
	}
	ReplyJSONOK(w, "ok")
}

func messages(w http.ResponseWriter, r *http.Request, polledSet *PolledSet) {
	id := mux.Vars(r)["connId"]
	conn := polledSet.GetConnection(id)
	if conn == nil {
		http.NotFound(w, r)
		return
	}
	start := 0
	count := -1 // All messages
	if sStr := r.FormValue("start"); sStr != "" {
		i, err := strconv.Atoi(sStr)
		if err != nil {
			log.Println("Invalid start value: ", sStr)
			http.Error(w, "Invalid value for start field", http.StatusBadRequest)
			return
		}
		start = i
	}
	if cStr := r.FormValue("count"); cStr != "" {
		i, err := strconv.Atoi(cStr)
		if err != nil {
			log.Println("Invalid count value: ", cStr)
			http.Error(w, "Invalid value for count field", http.StatusBadRequest)
			return
		}
		count = i
	}
	ReplyJSONOK(w, conn.GetMessages(start, count))
}
