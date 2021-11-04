// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
//     Unless required by applicable law or agreed to in writing, software
//     distributed under the License is distributed on an "AS IS" BASIS,
//     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//     See the License for the specific language governing permissions and
//     limitations under the License.

package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"strconv"

	"github.com/gorilla/mux"
)

func main() {
	pool := NewDevicePool()
	polledSet := NewPolledSet()
	var config interface{}
	err := json.Unmarshal([]byte("{\"ice_servers\":[{\"urls\":[\"stun:stun.l.google.com:19302\"]}],\"message_type\":\"config\"}"), &config)
	if err != nil {
		log.Fatal("Malformed config string")
	}

	setupDeviceEndpoint(pool, config)
	r := setupServerRoutes(pool, polledSet, config)

	http.Handle("/", r)
	if err := http.ListenAndServe(":8080", nil); err != nil {
		log.Fatal("ListenAndServe client: ", err)
	}
}

func setupDeviceEndpoint(pool *DevicePool, config interface{}) {
	devMux := http.NewServeMux()
	if devMux == nil {
		log.Fatal("Failed to allocate ServeMux")
	}
	// http.HandleFunc("/", serveHome)
	devMux.HandleFunc("/register_device", func(w http.ResponseWriter, r *http.Request) {
		deviceWs(w, r, pool, config)
	})
	// Serve the register_device endpoint in a different thread
	go func() {
		err := http.ListenAndServe("127.0.0.1:8081", devMux)
		if err != nil {
			log.Fatal("ListenAndServe device: ", err)
		}
	}()
}

func setupServerRoutes(pool *DevicePool, polledSet *PolledSet, config interface{}) *mux.Router {
	router := mux.NewRouter()
	http.HandleFunc("/connect_client", func(w http.ResponseWriter, r *http.Request) {
		clientWs(w, r, pool, config)
	})
	// The path parameter needs to include the leading '/'
	router.HandleFunc("/devices/{deviceId}/files{path:/.+}", func(w http.ResponseWriter, r *http.Request) {
		deviceFiles(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/devices", func(w http.ResponseWriter, r *http.Request) {
		listDevices(w, r, pool)
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
		replyJSON(w, config)
	}).Methods("GET")
	fs := http.FileServer(http.Dir("static"))
	router.PathPrefix("/").Handler(fs)
	return router
}

// Device endpoint
func deviceWs(w http.ResponseWriter, r *http.Request, pool *DevicePool, config interface{}) {
	log.Println(r.URL)
	ws := NewJsonWs(w, r)
	if ws == nil {
		return
	}
	// Serve the websocket in its own thread
	go func() {
		defer ws.Close()
		var msg RegisterMsg
		ok := ws.Recv(&msg)
		if !ok {
			wsReplyError(ws, "Websocket closed unexpectedly")
			return
		}
		if msg.Type != "register" {
			wsReplyError(ws, "First device message must be the registration")
			return
		}
		id := msg.DeviceId
		if id == "" {
			wsReplyError(ws, "Missing device_id")
			return
		}
		info := msg.Info
		if info == nil {
			log.Println("No device info provided by: ", id)
			info = make(map[string]interface{})
		}
		port := msg.Port
		device := NewDevice(ws, id, port, info)
		if !pool.Register(device, id) {
			wsReplyError(ws, fmt.Sprintln("Device id already taken: ", id))
			return
		}
		defer pool.Unregister(id)
		if !device.Send(config) {
			log.Println("Failed to send config to device")
			return
		}
		for {
			var msg ForwardMsg
			ok := ws.Recv(&msg)
			if !ok {
				log.Println("Device websocket closed")
				return
			}
			if msg.Type != "forward" {
				wsReplyError(ws, fmt.Sprintln("Unrecognized message type: ", msg.Type))
				return
			}
			clientId := msg.ClientId
			if clientId == 0 {
				wsReplyError(ws, "Device forward message missing client id")
				return
			}
			payload := msg.Payload
			if payload == nil {
				wsReplyError(ws, "Device forward message missing payload")
				return
			}
			dMsg := map[string]interface{}{
				"message_type": "device_msg",
				"payload":      payload,
			}
			if !device.ToClient(clientId, dMsg) {
				log.Println(ws, "Missing client ", int(clientId), " for device ", id)
				wsReplyError(ws, fmt.Sprintln("Client disconnected: ", clientId))
			}
		}
	}()
}

// General client endpoints

func listDevices(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	if err := replyJSON(w, pool.DeviceIds()); err != nil {
		log.Println(err)
	}
}

func deviceFiles(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	vars := mux.Vars(r)
	devId := vars["deviceId"]
	dev := pool.GetDevice(devId)
	if dev == nil {
		http.NotFound(w, r)
		return
	}
	path := vars["path"]
	if shouldIntercept(path) {
		http.ServeFile(w, r, fmt.Sprintf("intercept%s", path))
	} else {
		r.URL.Path = path
		dev.Proxy.ServeHTTP(w, r)
	}
}

// Client websocket endpoint

func clientWs(w http.ResponseWriter, r *http.Request, pool *DevicePool, config interface{}) {
	log.Println(r.URL)
	ws := NewJsonWs(w, r)
	if ws == nil {
		return
	}
	// Serve the websocket in its own thread
	go func() {
		defer ws.Close()
		var msg ConnectMsg
		ok := ws.Recv(&msg)
		if !ok {
			log.Println("Websocket closed unexpectedly")
			return
		}
		if msg.Type != "connect" {
			wsReplyError(ws, "First client message must be 'connect'")
			return
		}
		deviceId := msg.DeviceId
		if deviceId == "" {
			wsReplyError(ws, "Missing or invalid device_id")
			return
		}
		device := pool.GetDevice(deviceId)
		if device == nil {
			wsReplyError(ws, fmt.Sprintln("Unknown device id: ", deviceId))
			return
		}
		client := NewWsClient(ws)
		id := device.Register(client)
		defer device.Unregister(id)
		if !client.Send(config) {
			log.Println("Failed to send config to client")
			return
		}
		infoMsg := make(map[string]interface{})
		infoMsg["message_type"] = "device_info"
		infoMsg["device_info"] = device.info
		if !client.Send(infoMsg) {
			log.Println("Failed to send device info to client")
			return
		}
		for {
			var msg ForwardMsg
			ok := ws.Recv(&msg)
			if !ok {
				log.Println("Client websocket closed")
				return
			}
			if msg.Type != "forward" {
				wsReplyError(ws, fmt.Sprintln("Unrecognized message type: ", msg.Type))
				return
			}
			payload := msg.Payload
			if payload == nil {
				wsReplyError(ws, "Client forward message missing payload")
				return
			}
			cMsg := map[string]interface{}{
				"message_type": "client_msg",
				"client_id":    id,
				"payload":      payload,
			}
			if !device.Send(cMsg) {
				wsReplyError(ws, "Device disconnected")
			}
		}
	}()
}

// Http long polling client endpoints

func createPolledConnection(w http.ResponseWriter, r *http.Request, pool *DevicePool, polledSet *PolledSet) {
	var msg NewConnMsg
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
	reply := NewConnReply{ConnId: conn.Id(), DeviceInfo: device.info}
	replyJSON(w, reply)
}

func forward(w http.ResponseWriter, r *http.Request, polledSet *PolledSet) {
	id := mux.Vars(r)["connId"]
	conn := polledSet.GetConnection(id)
	if conn == nil {
		http.NotFound(w, r)
		return
	}
	var msg ForwardMsg
	err := json.NewDecoder(r.Body).Decode(&msg)
	if err != nil {
		log.Println("Failed to parse json from client: ", err)
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	cMsg := map[string]interface{}{
		"message_type": "client_msg",
		"client_id":    conn.ClientId(),
		"payload":      msg.Payload,
	}
	if !conn.ToDevice(cMsg) {
		http.Error(w, "Device disconnected", http.StatusNotFound)
	}
	replyJSON(w, "ok")
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
	replyJSON(w, conn.GetMessages(start, count))
}

// JSON objects schema

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

// Utility functions

// Log and reply with an error over a websocket
func wsReplyError(ws *JsonWs, msg string) {
	log.Println(msg)
	ws.Send(ErrorMsg{Error: msg})
}

// Send a JSON http response to the client
func replyJSON(w http.ResponseWriter, obj interface{}) error {
	w.Header().Set("Content-Type", "application/json")
	encoder := json.NewEncoder(w)
	return encoder.Encode(obj)
}

// Whether a device file request should be intercepted and served from the signaling server instead
func shouldIntercept(path string) bool {
	_, err := os.Stat(fmt.Sprintf("intercept/%s", path))
	if err != nil && os.IsNotExist(err) {
		return false
	}
	if err != nil {
		log.Println("Error accessing intercepted file: ", err)
	}
	return true
}
