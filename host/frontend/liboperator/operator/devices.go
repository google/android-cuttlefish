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
	"errors"
	"fmt"
	"log"
	"net/http/httputil"
	"net/url"
	"sync"
)

type Device struct {
	info interface{}
	conn *JSONUnix
	// Reverse proxy to the client files
	Proxy *httputil.ReverseProxy
	// Synchronizes access to the client list and the client count
	clientsMtx sync.Mutex
	clients    map[int]Client
	// Historical client count, it doesn't decrease when clients unregister. It's used to generate client ids.
	clientCount int
}

func NewDevice(conn *JSONUnix, port int, info interface{}) *Device {
	url, err := url.Parse(fmt.Sprintf("http://127.0.0.1:%d", port))
	if err != nil {
		// This should not happen
		log.Fatal("Unable to parse hard coded url: ", err)
		return nil
	}
	proxy := httputil.NewSingleHostReverseProxy(url)
	return &Device{conn: conn, Proxy: proxy, info: info, clients: make(map[int]Client), clientCount: 0}
}

// Sends a message to the device
func (d *Device) Send(msg interface{}) error {
	return d.conn.Send(msg)
}

func (d *Device) Register(c Client) int {
	d.clientsMtx.Lock()
	defer d.clientsMtx.Unlock()
	d.clientCount += 1
	d.clients[d.clientCount] = c
	return d.clientCount
}

func (d *Device) Unregister(id int) {
	d.clientsMtx.Lock()
	defer d.clientsMtx.Unlock()
	delete(d.clients, id)
}

// Notify the clients that the device has disconnected
func (d *Device) DisconnectClients() {
	d.clientsMtx.Lock()
	clientsCopy := make([]Client, 0, len(d.clients))
	for _, client := range d.clients {
		clientsCopy = append(clientsCopy, client)
	}
	d.clientsMtx.Unlock()
	for _, client := range clientsCopy {
		client.OnDeviceDisconnected()
	}
}

// Send a message to the client
func (d *Device) ToClient(id int, msg interface{}) error {
	d.clientsMtx.Lock()
	client, ok := d.clients[id]
	d.clientsMtx.Unlock()
	if !ok {
		return errors.New(fmt.Sprint("Unknown client: ", id))
	}
	return client.Send(msg)
}

// Keeps track of the registered devices.
type DevicePool struct {
	devicesMtx sync.Mutex
	devices    map[string]*Device
}

func NewDevicePool() *DevicePool {
	return &DevicePool{
		devices: make(map[string]*Device),
	}
}

// Register a new device, returns false if a device with that id already exists
func (p *DevicePool) Register(d *Device, id string) bool {
	p.devicesMtx.Lock()
	defer p.devicesMtx.Unlock()
	_, ok := p.devices[id]
	if ok {
		return false
	}
	p.devices[id] = d
	return true
}

func (p *DevicePool) Unregister(id string) {
	p.devicesMtx.Lock()
	defer p.devicesMtx.Unlock()
	if d, ok := p.devices[id]; ok {
		d.DisconnectClients()
		delete(p.devices, id)
	}
}

func (p *DevicePool) GetDevice(id string) *Device {
	p.devicesMtx.Lock()
	defer p.devicesMtx.Unlock()
	return p.devices[id]
}

// List the registered devices' ids
func (p *DevicePool) DeviceIds() []string {
	p.devicesMtx.Lock()
	defer p.devicesMtx.Unlock()
	ret := make([]string, 0, len(p.devices))
	for key, _ := range p.devices {
		ret = append(ret, key)
	}
	return ret
}
