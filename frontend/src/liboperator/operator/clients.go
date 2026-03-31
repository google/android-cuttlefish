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
	"math/rand"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// WebSocket timing constants
const (
	// Time allowed to write a message to the peer
	wsWriteWait = 10 * time.Second
	// Time allowed to read the next pong message from the peer
	wsPongWait = 60 * time.Second
	// Send pings to peer with this period (must be less than wsPongWait)
	wsPingPeriod = 30 * time.Second
	// Maximum message size allowed from peer
	wsMaxMessageSize = 64 * 1024
)

type Client interface {
	// Send a message to the device
	Send(msg interface{}) error
	// Provides an opportunity for the client to react to the device being disconnected
	OnDeviceDisconnected()
}

// Implements the client interface using a polled connection
type PolledClient struct {
	// The polled connection id
	id       string
	device   *Device
	messages []interface{}
	// Synchronizes access to the messages list
	msgMtx sync.Mutex
	// The id given to this client by the device
	clientId int
	// A reference to the polled set to clean up when the device disconnects
	polledSet *PolledSet
}

// From IClient
func (c *PolledClient) Send(msg interface{}) error {
	c.msgMtx.Lock()
	defer c.msgMtx.Unlock()
	c.messages = append(c.messages, msg)
	return nil
}
func (c *PolledClient) OnDeviceDisconnected() {
	// This object is useless when the device disconnects, release all resourcess associated to it
	c.polledSet.Destroy(c.id)
}

// Sends a message to the device
func (c *PolledClient) ToDevice(msg interface{}) error {
	return c.device.Send(msg)
}

// Gets count messages from the device, skipping the first start messages
func (c *PolledClient) GetMessages(start int, count int) []interface{} {
	c.msgMtx.Lock()
	defer c.msgMtx.Unlock()
	if count < 0 {
		count = len(c.messages) - start
	}
	end := start + count
	if end > len(c.messages) {
		end = len(c.messages)
		count = end - start
	}
	ret := make([]interface{}, count)
	copy(ret, c.messages[start:end])
	return ret
}

func (c *PolledClient) Id() string {
	return c.id
}

func (c *PolledClient) ClientId() int {
	return c.clientId
}

// Basically a map of polled clients by id.
type PolledSet struct {
	connections map[string]*PolledClient
	mapMtx      sync.Mutex
	rand        *rand.Rand
}

func (s *PolledSet) NewConnection(d *Device) *PolledClient {
	s.mapMtx.Lock()
	defer s.mapMtx.Unlock()
	for {
		id := randStr(64, s.rand)
		if _, ok := s.connections[id]; !ok {
			conn := &PolledClient{
				id:        id,
				device:    d,
				messages:  make([]interface{}, 0),
				polledSet: s,
			}
			s.connections[id] = conn
			conn.clientId = d.Register(conn)
			return conn
		}
	}
}

func (s *PolledSet) GetConnection(id string) *PolledClient {
	s.mapMtx.Lock()
	defer s.mapMtx.Unlock()
	return s.connections[id]
}

func (s *PolledSet) Destroy(id string) {
	s.mapMtx.Lock()
	defer s.mapMtx.Unlock()
	delete(s.connections, id)
}

func NewPolledSet() *PolledSet {
	return &PolledSet{
		connections: make(map[string]*PolledClient),
		rand:        rand.New(rand.NewSource(time.Now().UnixNano())),
	}
}

// The available characters for the connection ids
const runes = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

func randStr(l int, r *rand.Rand) string {
	var s strings.Builder
	for i := 0; i < l; i++ {
		s.WriteByte(runes[r.Intn(len(runes))])
	}
	return s.String()
}

// WsClient implements Client for WebSocket connections.
type WsClient struct {
	// The id given to this client by the device
	clientId int
	conn     *websocket.Conn
	device   *Device
	// Device id for logging
	deviceId string
	// Buffered channel of outbound messages
	send chan interface{}
	// Signals that the connection is closed
	done chan struct{}
	// Ensures Close() only runs once
	closeOnce sync.Once
}

// NewWsClient creates a new WebSocket client and starts its read/write pumps.
func NewWsClient(conn *websocket.Conn, device *Device, deviceId string) *WsClient {
	c := &WsClient{
		conn:     conn,
		device:   device,
		deviceId: deviceId,
		send:     make(chan interface{}, 256),
		done:     make(chan struct{}),
	}
	go c.writePump()
	go c.readPump()
	return c
}

// Send queues a message for delivery to the WebSocket client.
func (c *WsClient) Send(msg interface{}) error {
	// Check if already closed before attempting to send. Without this,
	// select would pick randomly between send and done when both are ready.
	select {
	case <-c.done:
		return fmt.Errorf("WebSocket connection closed")
	default:
	}
	select {
	case c.send <- msg:
		return nil
	case <-c.done:
		return fmt.Errorf("WebSocket connection closed")
	}
}

// OnDeviceDisconnected notifies the client that the device disconnected.
func (c *WsClient) OnDeviceDisconnected() {
	// Send an error message to the client before closing
	c.Send(map[string]interface{}{
		"error": "Device disconnected",
	})
	c.Close()
}

// Close signals shutdown of the WebSocket connection.
// The actual connection close is handled by writePump's defer.
func (c *WsClient) Close() {
	c.closeOnce.Do(func() {
		close(c.done)
	})
}

// Done returns a channel that is closed when the client disconnects.
func (c *WsClient) Done() <-chan struct{} {
	return c.done
}

// ClientId returns the id assigned by the device.
func (c *WsClient) ClientId() int {
	return c.clientId
}

// readPump reads messages from the WebSocket and forwards them to the device.
func (c *WsClient) readPump() {
	defer func() {
		log.Printf("WebSocket client %d disconnected from device %s",
			c.clientId, c.deviceId)
		c.device.Unregister(c.clientId)
		c.Close()
	}()

	c.conn.SetReadLimit(wsMaxMessageSize)
	c.conn.SetReadDeadline(time.Now().Add(wsPongWait))
	c.conn.SetPongHandler(func(string) error {
		c.conn.SetReadDeadline(time.Now().Add(wsPongWait))
		return nil
	})

	for {
		_, message, err := c.conn.ReadMessage()
		if err != nil {
			if !websocket.IsCloseError(err,
				websocket.CloseNormalClosure,
				websocket.CloseGoingAway) {
				log.Printf("WebSocket read error: %v", err)
			}
			return
		}

		var msg map[string]interface{}
		if err := json.Unmarshal(message, &msg); err != nil {
			log.Printf("Invalid JSON from WebSocket client: %v", err)
			return
		}

		// Forward to device as client_msg
		clientMsg := map[string]interface{}{
			"message_type": "client_msg",
			"client_id":    c.clientId,
			"payload":      msg,
		}
		if err := c.device.Send(clientMsg); err != nil {
			log.Printf("Failed to forward message to device: %v", err)
			return
		}
	}
}

// writePump writes messages from the send channel to the WebSocket.
func (c *WsClient) writePump() {
	ticker := time.NewTicker(wsPingPeriod)
	defer func() {
		ticker.Stop()
		c.conn.Close()
	}()

	for {
		select {
		case msg, ok := <-c.send:
			c.conn.SetWriteDeadline(time.Now().Add(wsWriteWait))
			if !ok {
				// Channel closed
				c.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}

			if err := c.conn.WriteJSON(msg); err != nil {
				log.Printf("WebSocket write error: %v", err)
				return
			}

		case <-ticker.C:
			c.conn.SetWriteDeadline(time.Now().Add(wsWriteWait))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}

		case <-c.done:
			return
		}
	}
}
