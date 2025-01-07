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
	"context"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"strconv"
	"strings"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/gorilla/mux"
	"github.com/gorilla/websocket"

	gopb "github.com/google/android-cuttlefish/frontend/src/liboperator/protobuf"
	grpcpb "github.com/google/android-cuttlefish/frontend/src/liboperator/protobuf"
	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

func createUnixSocketEndpoint(path string) (*net.UnixListener, error) {
	if err := os.RemoveAll(path); err != nil {
		return nil, fmt.Errorf("Failed to clean previous socket: %w", err)
	}
	addr, err := net.ResolveUnixAddr("unixpacket", path)
	if err != nil {
		// Returns a loop function that will immediately return an error when invoked
		return nil, fmt.Errorf("Failed to create unix address from path: %w", err)
	}
	sock, err := net.ListenUnix("unixpacket", addr)
	if err != nil {
		return nil, fmt.Errorf("Failed to create unix socket: %w", err)
	}
	// Make sure the socket is only accessible by owner and group
	if err := os.Chmod(path, 0770); err != nil {
		// This shouldn't happen since the creation of the socket just succeeded
		log.Println("Failed to change permissions on socket file: ", err)
	}
	return sock, err
}

// Sets up a unix socket for devices to connect to and returns a function that listens on the
// socket until an error occurs.
func SetupDeviceEndpoint(pool *DevicePool, config apiv1.InfraConfig, path string) (func() error, error) {
	sock, err := createUnixSocketEndpoint(path)
	if err != nil {
		return nil, err
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
	}, nil
}

// Sets up a unix socket for server control and returns a function that listens on the socket
// until an error occurs.
func SetupControlEndpoint(pool *DevicePool, path string) (func() error, error) {
	sock, err := createUnixSocketEndpoint(path)
	if err != nil {
		return nil, err
	}
	log.Println("Control endpoint created")
	// Serve the register_device endpoint in a background thread
	return func() error {
		defer sock.Close()
		for {
			c, err := sock.AcceptUnix()
			if err != nil {
				return err
			}
			go controlEndpoint(NewJSONUnix(c), pool)
		}
	}, nil
}

// Creates a router with handlers for the following endpoints:
// GET  /infra_config
// GET	/groups
// GET  /devices
// GET  /devices?groupId={groupId}
// GET  /devices/{deviceId}
// GET  /devices/{deviceId}/files/{path}
// GET  /devices/{deviceId}/services
// GET  /devices/{deviceId}/services/{serviceName}
// GET  /devices/{deviceId}/services/{serviceName}/{methodName}
// POST /devices/{deviceId}/services/{serviceName}/{methodName}
// GET  /devices/{deviceId}/services/{serviceName}/{typeName}/type
// GET  /devices/{deviceId}/openwrt{path:/.*}
// POST /devices/{deviceId}/openwrt{path:/.*}
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
	router.HandleFunc("/groups", func(w http.ResponseWriter, r *http.Request) {
		listGroups(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/devices/{deviceId}/files{path:/.+}", func(w http.ResponseWriter, r *http.Request) {
		deviceFiles(w, r, pool, maybeIntercept)
	}).Methods("GET")
	router.HandleFunc("/devices", func(w http.ResponseWriter, r *http.Request) {
		listDevices(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/devices/{deviceId}", func(w http.ResponseWriter, r *http.Request) {
		deviceInfo(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/devices/{deviceId}/services", func(w http.ResponseWriter, r *http.Request) {
		grpcListServices(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/devices/{deviceId}/services/{serviceName}", func(w http.ResponseWriter, r *http.Request) {
		grpcListMethods(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/devices/{deviceId}/services/{serviceName}/{methodName}", func(w http.ResponseWriter, r *http.Request) {
		grpcListReqResType(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/devices/{deviceId}/services/{serviceName}/{methodName}", func(w http.ResponseWriter, r *http.Request) {
		grpcCallUnaryMethod(w, r, pool)
	}).Methods("POST")
	router.HandleFunc("/devices/{deviceId}/services/{serviceName}/{typeName}/type", func(w http.ResponseWriter, r *http.Request) {
		grpcTypeInformation(w, r, pool)
	}).Methods("GET")
	router.HandleFunc("/devices/{deviceId}/openwrt{path:/.*}", func(w http.ResponseWriter, r *http.Request) {
		openwrt(w, r, pool)
	}).Methods("GET", "POST")
	router.HandleFunc("/devices/{deviceId}/adb", func(w http.ResponseWriter, r *http.Request) {
		adbProxy(w, r, pool)
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

// Control endpoint
func controlEndpoint(c *JSONUnix, pool *DevicePool) {
	log.Println("Controller connected")
	defer c.Close()

	msg := make(map[string]interface{})
	if err := c.Recv(&msg); err != nil {
		log.Println("Error reading control message: ", err)
		return
	}
	typeRaw, ok := msg["message_type"]
	if !ok {
		log.Println("Control message doesn't include message_type: ", msg)
		return
	}
	typeStr, ok := typeRaw.(string)
	if !ok {
		log.Println("Control message's message_type field is not a string: ", msg)
		return
	}
	switch typeStr {
	case "pre-register":
		var preRegisterMsg apiv1.PreRegisterMsg
		// Ignore errors, msg was parsed from json so this can't fail
		j, _ := json.Marshal(msg)
		json.Unmarshal(j, &preRegisterMsg)
		PreRegister(c, pool, &preRegisterMsg)
	default:
		log.Println("Invalid control type: ", typeStr)
		return
	}
}

func PreRegister(c *JSONUnix, pool *DevicePool, msg *apiv1.PreRegisterMsg) {
	var ret apiv1.PreRegistrationResponse
	idSet := make(map[string]bool)
	// Length ensures no writers will block on this channel
	devCh := make(chan string, len(msg.Devices))
	for _, d := range msg.Devices {
		regCh := make(chan bool, 1)
		err := pool.PreRegister(
			&apiv1.DeviceDescriptor{
				DeviceId:  d.Id,
				GroupName: msg.GroupName,
				Owner:     msg.Owner,
				Name:      d.Name,
				ADBPort:   d.ADBPort,
			},
			regCh,
		)
		status := apiv1.RegistrationStatusReport{
			Id:     d.Id,
			Status: "accepted",
		}
		if err != nil {
			status.Status = "failed"
			status.Msg = err.Error()
		} else {
			idSet[d.Id] = true
			go func(regCh chan bool, devCh chan string, id string) {
				registered, ok := <-regCh
				// The channel is closed if cancelled, true is sent when registration succeeds.
				if registered && ok {
					devCh <- id
				}
			}(regCh, devCh, d.Id)
		}
		ret = append(ret, status)
	}

	if err := c.Send(ret); err != nil {
		log.Println("Error sending pre-registration response: ", err)
		return
	}

	closeCh := make(chan bool)
	go func(c *JSONUnix, ch chan bool) {
		// Wait until socket closes or produces another error
		var err error
		for err == nil {
			var ignored interface{}
			err = c.Recv(ignored)
		}
		ch <- true
	}(c, closeCh)

	for {
		select {
		case id := <-devCh:
			evt := apiv1.RegistrationStatusReport{
				Id:     id,
				Status: "registered",
			}
			if err := c.Send(&evt); err != nil {
				log.Println("Error sending registration event: ", err)
			}
			delete(idSet, id)
		case <-closeCh:
			// Cancel all pending pre-registrations when this function returns
			for id := range idSet {
				pool.CancelPreRegistration(id)
			}
			return
		}
	}
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
	device := pool.Register(id, c, port, info)
	if device == nil {
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

func grpcListServices(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	vars := mux.Vars(r)
	conn, err := ConnectControlEnvProxyServer(vars["deviceId"], pool)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	defer conn.Close()

	client := grpcpb.NewControlEnvProxyServiceClient(conn)
	reply, err := client.ListServices(context.Background(), &emptypb.Empty{})
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	ReplyJSONOK(w, reply)
}

func grpcListMethods(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	vars := mux.Vars(r)
	conn, err := ConnectControlEnvProxyServer(vars["deviceId"], pool)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	defer conn.Close()

	request := gopb.ListMethodsRequest{
		ServiceName: vars["serviceName"],
	}
	client := grpcpb.NewControlEnvProxyServiceClient(conn)
	reply, err := client.ListMethods(context.Background(), &request)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	ReplyJSONOK(w, reply)
}

func grpcListReqResType(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	vars := mux.Vars(r)
	conn, err := ConnectControlEnvProxyServer(vars["deviceId"], pool)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	defer conn.Close()

	request := gopb.ListReqResTypeRequest{
		ServiceName: vars["serviceName"],
		MethodName:  vars["methodName"],
	}
	client := grpcpb.NewControlEnvProxyServiceClient(conn)
	reply, err := client.ListReqResType(context.Background(), &request)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	ReplyJSONOK(w, reply)
}

func grpcCallUnaryMethod(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	vars := mux.Vars(r)
	conn, err := ConnectControlEnvProxyServer(vars["deviceId"], pool)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	defer conn.Close()

	body, err := ioutil.ReadAll(r.Body)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	request := gopb.CallUnaryMethodRequest{
		ServiceName:        vars["serviceName"],
		MethodName:         vars["methodName"],
		JsonFormattedProto: string(body),
	}
	client := grpcpb.NewControlEnvProxyServiceClient(conn)
	reply, err := client.CallUnaryMethod(context.Background(), &request)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	ReplyJSONOK(w, reply.JsonFormattedProto)
}

func grpcTypeInformation(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	vars := mux.Vars(r)
	conn, err := ConnectControlEnvProxyServer(vars["deviceId"], pool)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	defer conn.Close()

	request := gopb.TypeInformationRequest{
		ServiceName: vars["serviceName"],
		TypeName:    vars["typeName"],
	}
	client := grpcpb.NewControlEnvProxyServiceClient(conn)
	reply, err := client.TypeInformation(context.Background(), &request)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	w.Header().Set("Content-Type", "text/plain")
	w.WriteHeader(http.StatusOK)
	w.Write([]byte(reply.TextFormattedTypeInfo))
}

func openwrt(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	vars := mux.Vars(r)
	devId := vars["deviceId"]
	dev := pool.GetDevice(devId)
	if dev == nil {
		http.Error(w, "Device not found", http.StatusNotFound)
		return
	}

	devInfo := dev.privateData.(map[string]interface{})
	openwrtDevId, ok := devInfo["openwrt_device_id"].(string)
	if !ok {
		http.Error(w, "Device obtaining Openwrt not found", http.StatusNotFound)
		return
	}

	path := vars["path"]
	openwrtAddr, ok := devInfo["openwrt_addr"].(string)
	if !ok {
		http.Error(w, "Openwrt address not found", http.StatusNotFound)
		return
	}

	url, _ := url.Parse("http://" + openwrtAddr)
	proxy := httputil.NewSingleHostReverseProxy(url)
	proxy.ErrorHandler = func(w http.ResponseWriter, r *http.Request, err error) {
		log.Printf("request %q failed: proxy error: %v", r.Method+" "+r.URL.Path, err)
		w.Header().Add("x-cutf-proxy", "op-openwrt")
		w.WriteHeader(http.StatusBadGateway)
	}
	r.URL.Path = "/devices/" + openwrtDevId + "/openwrt" + path
	proxy.ServeHTTP(w, r)
}

// WebSocket endpoint that proxies ADB
func adbProxy(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	vars := mux.Vars(r)
	devId := vars["deviceId"]
	dev := pool.GetDevice(devId)

	if dev == nil {
		http.Error(w, "Device not found", http.StatusNotFound)
		return
	}

	devInfo := dev.privateData.(map[string]interface{})

	// Find adb port for the device.
	adbPort := dev.Descriptor.ADBPort
	if adbPort == 0 {
		// ADB port might not be set if the device isn't started by cvd,
		// some newer versions set it in the device info, make one last
		// ditch attempt at finding it.
		if adb_port, ok := devInfo["adb_port"]; ok {
			adbPort = int(adb_port.(float64))
		} else {
			http.Error(w, "Cannot find adb port for the device", http.StatusNotFound)
			return
		}
	}

	// Prepare WebSocket and TCP socket for ADB
	tcpConn, err := net.Dial("tcp", fmt.Sprintf("127.0.0.1:%v", adbPort))
	if err != nil {
		log.Print("Error while connect to ADB: ", err)
		return
	}
	defer tcpConn.Close()

	upgrader := websocket.Upgrader{}
	wsConn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Print("Error while upgrading to WebSocket: ", err)
		return
	}
	wsWrapper := &wsIoWrapper{
		wsConn: wsConn,
		pos:    0,
		buf:    nil,
	}

	// Redirect WebSocket to ADB tcp socket
	go func() {
		// TODO: Replace with checking net.ErrClosed after Go 1.16
		if _, err := io.Copy(wsWrapper, tcpConn); err != nil && !strings.Contains(err.Error(), "use of closed network connection") {
			log.Print("Error while io.Copy from ADB to WebSocket: ", err)
		}
		if err = wsWrapper.Close(); err != nil {
			log.Print("Error while closing WebSocket: ", err)
		}
	}()
	if _, err = io.Copy(tcpConn, wsWrapper); err != nil {
		log.Print("Error while io.Copy from WebSocket to ADB: ", err)
	}
}

// Wrapper for implementing io.ReadWriteCloser of websocket.Conn
type wsIoWrapper struct {
	wsConn *websocket.Conn
	// Next position to read from buf
	pos int
	// Current chunk of data which was read from WebSocket
	buf []byte
}

var _ io.ReadWriteCloser = (*wsIoWrapper)(nil)

func (w *wsIoWrapper) Read(p []byte) (int, error) {
	if w.buf == nil || w.pos >= len(w.buf) {
		_, readBuf, err := w.wsConn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway) {
				return 0, io.EOF
			}
			return 0, err
		}
		w.buf = readBuf
		w.pos = 0
	}
	nRead := copy(p, w.buf[w.pos:])
	w.pos += nRead
	return nRead, nil
}

func (w *wsIoWrapper) Write(buf []byte) (int, error) {
	err := w.wsConn.WriteMessage(websocket.BinaryMessage, buf)
	if err != nil {
		return 0, err
	}
	return len(buf), nil
}

func (w *wsIoWrapper) Close() error {
	return w.wsConn.Close()
}

// General client endpoints
func listGroups(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	if err := ReplyJSONOK(w, pool.GroupIds()); err != nil {
		log.Println(err)
	}
}

func listDevices(w http.ResponseWriter, r *http.Request, pool *DevicePool) {
	groupId := r.URL.Query().Get("groupId")

	if len(groupId) == 0 {
		if err := ReplyJSONOK(w, pool.GetDeviceDescList()); err != nil {
			log.Println(err)
		}
		return
	}

	if err := ReplyJSONOK(w, pool.GetDeviceDescByGroupId(groupId)); err != nil {
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
	ReplyJSONOK(w, apiv1.DeviceInfoReply{DeviceDescriptor: dev.Descriptor, RegistrationInfo: dev.privateData})
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
	reply := apiv1.NewConnReply{ConnId: conn.Id(), DeviceInfo: device.privateData}
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
