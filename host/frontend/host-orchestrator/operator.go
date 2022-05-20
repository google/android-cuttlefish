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

package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"math/rand"
	"net"
	"net/http"
	"os"
	"strconv"
	"sync"
	"time"

	apiv1 "cuttlefish/host-orchestrator/api/v1"

	"github.com/google/uuid"
	"github.com/gorilla/mux"
)

const (
	defaultSocketPath             = "/run/cuttlefish/operator"
	defaultHttpPort               = "1080"
	defaultHttpsPort              = "1443"
	defaultTLSCertDir             = "/etc/cuttlefish-common/host-orchestrator/cert"
	defaultInstanceManagerEnabled = false
	defaultAndroidBuildURL        = "https://androidbuildinternal.googleapis.com"
	defaultCVDArtifactsDir        = "/var/lib/cuttlefish-common"
)

func startHttpServer() {
	httpPort := fromEnvOrDefault("ORCHESTRATOR_HTTP_PORT", defaultHttpPort)
	log.Println(fmt.Sprint("Host Orchestrator is listening at http://localhost:", httpPort))

	log.Fatal(http.ListenAndServe(
		fmt.Sprint(":", httpPort),
		// handler is nil, so DefaultServeMux is used.
		nil))
}

func startHttpsServer() {
	tlsCertDir := fromEnvOrDefault("ORCHESTRATOR_TLS_CERT_DIR", defaultTLSCertDir)
	httpsPort := fromEnvOrDefault("ORCHESTRATOR_HTTPS_PORT", defaultHttpsPort)
	certPath := tlsCertDir + "/cert.pem"
	keyPath := tlsCertDir + "/key.pem"
	log.Println(fmt.Sprint("Host Orchestrator is listening at https://localhost:", httpsPort))
	log.Fatal(http.ListenAndServeTLS(fmt.Sprint(":", httpsPort),
		certPath,
		keyPath,
		// handler is nil, so DefaultServeMux is used.
		//
		// Using DefaultServerMux in both servers (http and https) is not a problem
		// as http.ServeMux instances are thread safe.
		nil))
}

func main() {
	socketPath := fromEnvOrDefault("ORCHESTRATOR_SOCKET_PATH", defaultSocketPath)
	imEnabled := fromEnvOrDefaultBool("ORCHESTRATOR_INSTANCE_MANAGER_ENABLED", defaultInstanceManagerEnabled)
	rand.Seed(time.Now().UnixNano())
	pool := NewDevicePool()
	polledSet := NewPolledSet()
	config := apiv1.InfraConfig{
		Type: "config",
		IceServers: []apiv1.IceServer{
			apiv1.IceServer{URLs: []string{"stun:stun.l.google.com:19302"}},
		},
	}
	abURL := fromEnvOrDefault("ORCHESTRATOR_ANDROID_BUILD_URL", defaultAndroidBuildURL)
	cvdArtifactsDir := fromEnvOrDefault("ORCHESTRATOR_CVD_ARTIFACTS_DIR", defaultCVDArtifactsDir)
	fetchCVDDownloader := NewABFetchCVDDownloader(http.DefaultClient, abURL)
	fetchCVDHandler := NewFetchCVDHandler(cvdArtifactsDir, fetchCVDDownloader)
	om := NewMapOM(func() string { return uuid.New().String() })
	im := NewInstanceManager(fetchCVDHandler, om)

	setupDeviceEndpoint(pool, config, socketPath)
	r := setupServerRoutes(pool, polledSet, config, imEnabled, im, om)
	http.Handle("/", r)

	starters := []func(){startHttpServer, startHttpsServer}
	wg := new(sync.WaitGroup)
	wg.Add(len(starters))
	for _, starter := range starters {
		go func(f func()) {
			defer wg.Done()
			f()
		}(starter)
	}
	wg.Wait()
}

func setupDeviceEndpoint(pool *DevicePool, config apiv1.InfraConfig, path string) {
	if err := os.RemoveAll(path); err != nil {
		log.Fatal("Failed to clean previous socket: ", err)
	}
	addr, err := net.ResolveUnixAddr("unixpacket", path)
	if err != nil {
		log.Println("Failed to create unix address from path: ", err)
		return
	}
	sock, err := net.ListenUnix("unixpacket", addr)
	if err != nil {
		log.Fatal("Failed to create unix socket: ", err)
	}
	// Make sure the socket is only accessible by owner and group
	if err := os.Chmod(path, 0770); err != nil {
		// This shouldn't happen since the creation of the socket just succeeded
		log.Println("Failed to change permissions on socket file: ", err)
	}
	log.Println("Device endpoint created")
	// Serve the register_device endpoint in a background thread
	go func() {
		defer sock.Close()
		for {
			c, err := sock.AcceptUnix()
			if err != nil {
				log.Fatal("Failed to accept: ", err)
			}
			go deviceEndpoint(NewJSONUnix(c), pool, config)
		}
	}()
}

func setupServerRoutes(
	pool *DevicePool,
	polledSet *PolledSet,
	config apiv1.InfraConfig,
	imEnabled bool,
	im *InstanceManager,
	om OperationManager) *mux.Router {
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
		replyJSONOK(w, config)
	}).Methods("GET")
	if imEnabled {
		router.HandleFunc("/devices", func(w http.ResponseWriter, r *http.Request) {
			createDevices(w, r, im)
		}).Methods("POST")
		router.HandleFunc("/operations/{name}", func(w http.ResponseWriter, r *http.Request) {
			getOperation(w, r, om)
		}).Methods("GET")
	}
	fs := http.FileServer(http.Dir("static"))
	router.PathPrefix("/").Handler(fs)
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
		replyError(c, "First device message must be the registration")
		return
	}
	id := msg.DeviceId
	if id == "" {
		replyError(c, "Missing device_id")
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
		replyError(c, fmt.Sprintln("Device id already taken: ", id))
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
			replyError(c, fmt.Sprintln("Unrecognized message type: ", msg.Type))
			return
		}
		clientId := msg.ClientId
		if clientId == 0 {
			replyError(c, "Device forward message missing client id")
			return
		}
		payload := msg.Payload
		if payload == nil {
			replyError(c, "Device forward message missing payload")
			return
		}
		dMsg := map[string]interface{}{
			"message_type": "device_msg",
			"payload":      payload,
		}
		if err := device.ToClient(clientId, dMsg); err != nil {
			log.Println("Device: ", id, " failed to send message to client: ", err)
			replyError(c, fmt.Sprintln("Client disconnected: ", clientId))
		}
	}
}

// General client endpoints

func listDevices(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	if err := replyJSONOK(w, pool.DeviceIds()); err != nil {
		log.Println(err)
	}
}

func createDevices(w http.ResponseWriter, r *http.Request, im *InstanceManager) {
	var msg apiv1.CreateCVDRequest
	err := json.NewDecoder(r.Body).Decode(&msg)
	if err != nil {
		replyJSONErr(w, NewBadRequestError("Malformed JSON in request", err))
		return
	}
	op, err := im.CreateCVD(msg)
	if err != nil {
		replyJSONErr(w, err)
		return
	}
	replyJSONOK(w, BuildOperation(op))
}

func getOperation(w http.ResponseWriter, r *http.Request, om OperationManager) {
	vars := mux.Vars(r)
	name := vars["name"]
	if op, err := om.Get(name); err != nil {
		replyJSONErr(w, NewNotFoundError("operation not found", err))
	} else {
		replyJSONOK(w, BuildOperation(op))
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

func clientWs(w http.ResponseWriter, r *http.Request, pool *DevicePool, config apiv1.InfraConfig) {
	log.Println(r.URL)
	ws := NewJSONWs(w, r)
	if ws == nil {
		return
	}
	// Serve the websocket in its own thread
	go func() {
		defer ws.Close()
		var msg apiv1.ConnectMsg
		if err := ws.Recv(&msg); err != nil {
			log.Println("Failed to receive from client: ", err)
			return
		}
		if msg.Type != "connect" {
			replyError(ws, "First client message must be 'connect'")
			return
		}
		deviceId := msg.DeviceId
		if deviceId == "" {
			replyError(ws, "Missing or invalid device_id")
			return
		}
		device := pool.GetDevice(deviceId)
		if device == nil {
			replyError(ws, fmt.Sprintln("Unknown device id: ", deviceId))
			return
		}
		client := NewWsClient(ws)
		id := device.Register(client)
		defer device.Unregister(id)
		if err := client.Send(config); err != nil {
			log.Println("Failed to send config to client: ", err)
			return
		}
		infoMsg := make(map[string]interface{})
		infoMsg["message_type"] = "device_info"
		infoMsg["device_info"] = device.info
		if err := client.Send(infoMsg); err != nil {
			log.Println("Failed to send device info to client: ", err)
			return
		}
		for {
			var msg apiv1.ForwardMsg
			if err := ws.Recv(&msg); err != nil {
				log.Println("Client websocket closed")
				return
			}
			if msg.Type != "forward" {
				replyError(ws, fmt.Sprintln("Unrecognized message type: ", msg.Type))
				return
			}
			payload := msg.Payload
			if payload == nil {
				replyError(ws, "Client forward message missing payload")
				return
			}
			cMsg := apiv1.ClientMsg{
				Type:     "client_msg",
				ClientId: id,
				Payload:  payload,
			}
			if err := device.Send(cMsg); err != nil {
				replyError(ws, "Device disconnected")
			}
		}
	}()
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
	replyJSONOK(w, reply)
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
	replyJSONOK(w, "ok")
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
	replyJSONOK(w, conn.GetMessages(start, count))
}

// Utility functions

// Interface implemented by any connection capable of sending in JSON format
type JSONConn interface {
	Send(val interface{}) error
}

// Log and reply with an error
func replyError(c JSONConn, msg string) {
	log.Println(msg)
	if err := c.Send(apiv1.ErrorMsg{Error: msg}); err != nil {
		log.Println("Failed to send error reply: ", err)
	}
}

// Send a JSON http response with success status code to the client
func replyJSONOK(w http.ResponseWriter, obj interface{}) error {
	return replyJSON(w, obj, http.StatusOK)
}

// Send a JSON http response with error to the client
func replyJSONErr(w http.ResponseWriter, err error) error {
	log.Printf("response with error: %v\n", err)
	var e *AppError
	if errors.As(err, &e) {
		return replyJSON(w, e.JSONResponse(), e.StatusCode)
	}
	return replyJSON(w, apiv1.ErrorMsg{Error: "Internal Server Error"}, http.StatusInternalServerError)
}

// Send a JSON http response to the client
func replyJSON(w http.ResponseWriter, obj interface{}, statusCode int) error {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	encoder := json.NewEncoder(w)
	return encoder.Encode(obj)
}

// Whether a device file request should be intercepted and served from the signaling server instead
func shouldIntercept(path string) bool {
	return path == "/js/server_connector.js"
}

func fromEnvOrDefault(key string, def string) string {
	val := os.Getenv(key)
	if val == "" {
		return def
	}
	return val
}

func fromEnvOrDefaultBool(key string, def bool) bool {
	val := os.Getenv(key)
	if val == "" {
		return def
	}
	b, err := strconv.ParseBool(val)
	if err != nil {
		panic(err)
	}
	return b
}

func BuildOperation(op Operation) apiv1.Operation {
	result := apiv1.Operation{
		Name: op.Name,
		Done: op.Done,
	}
	if !op.Done {
		return result
	}
	if op.IsError() {
		result.Result = &apiv1.OperationResult{
			Error: &apiv1.ErrorMsg{op.Result.Error.ErrorMsg},
		}
	}
	return result
}
