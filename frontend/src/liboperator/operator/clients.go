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
	"math/rand"
	"strings"
	"sync"
	"time"
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
