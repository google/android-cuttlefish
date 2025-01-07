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
	"net/http"
	"net/http/httputil"
	"net/url"
	"sync"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
)

type Device struct {
	// Provided by the device at registration time
	privateData interface{}
	// Provided at pre-registration time
	Descriptor apiv1.DeviceDescriptor
	conn       *JSONUnix
	// Reverse proxy to the client files
	Proxy *httputil.ReverseProxy
	// Synchronizes access to the client list and the client count
	clientsMtx sync.Mutex
	clients    map[int]Client
	// Historical client count, it doesn't decrease when clients unregister. It's used to generate client ids.
	clientCount int
}

type Group struct {
	deviceIds []string
}

const DEFAULT_GROUP_ID = "default"

func newDevice(id string, conn *JSONUnix, port int, privateData interface{}) *Device {
	url, err := url.Parse(fmt.Sprintf("http://127.0.0.1:%d", port))
	if err != nil {
		// This should not happen
		log.Fatal("Unable to parse hard coded url: ", err)
		return nil
	}
	proxy := httputil.NewSingleHostReverseProxy(url)
	proxy.ErrorHandler = func(w http.ResponseWriter, r *http.Request, err error) {
		log.Printf("request %q failed: proxy error: %v", r.Method+" "+r.URL.Path, err)
		w.Header().Add("x-cutf-proxy", "op-device")
		w.WriteHeader(http.StatusBadGateway)
	}
	groupId := groupIdFromPrivateData(privateData)
	return &Device{
		conn:        conn,
		Proxy:       proxy,
		privateData: privateData,
		Descriptor: apiv1.DeviceDescriptor{
			DeviceId:  id,
			GroupName: groupId,
		},
		clients:     make(map[int]Client),
		clientCount: 0,
	}
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

// preDevice holds information about a device between pre-registration and registration.
type preDevice struct {
	Desc  apiv1.DeviceDescriptor
	RegCh chan bool
}

// Keeps track of the registered devices.
type DevicePool struct {
	// preDevicesMtx must always be locked before devicesMtx
	preDevicesMtx sync.Mutex
	// Pre-registered devices by id
	preDevices map[string]*preDevice
	devicesMtx sync.Mutex
	devices    map[string]*Device
}

func NewDevicePool() *DevicePool {
	return &DevicePool{
		preDevices: make(map[string]*preDevice),
		devices:    make(map[string]*Device),
	}
}

func groupIdFromPrivateData(privateData interface{}) string {
	deviceInfo, ok := privateData.(map[string]interface{})
	if !ok {
		return DEFAULT_GROUP_ID
	}

	groupId, ok := deviceInfo["group_id"].(string)
	if !ok || len(groupId) == 0 {
		return DEFAULT_GROUP_ID
	}

	return groupId
}

// PreRegister accepts a channel of boolean which it closes if the pre-registration is cancelled or
// sends the output of registering the device as a boolean.
func (p *DevicePool) PreRegister(Descriptor *apiv1.DeviceDescriptor, regCh chan bool) error {
	d := &preDevice{
		Desc:  *Descriptor,
		RegCh: regCh,
	}
	p.preDevicesMtx.Lock()
	defer p.preDevicesMtx.Unlock()
	if _, found := p.preDevices[d.Desc.DeviceId]; found {
		return fmt.Errorf("Device Id already pre-registered")
	}
	// This locks devicesMtx after preDevicesMtx, which is the right order
	if d := p.GetDevice(d.Desc.DeviceId); d != nil {
		return fmt.Errorf("Device Id already registered")
	}
	p.preDevices[d.Desc.DeviceId] = d
	return nil
}

func (p *DevicePool) CancelPreRegistration(id string) {
	p.preDevicesMtx.Lock()
	defer p.preDevicesMtx.Unlock()
	if d, ok := p.preDevices[id]; ok {
		close(d.RegCh)
	}
	delete(p.preDevices, id)
}

// Register a new device, returns false if a device with that id already exists
func (p *DevicePool) Register(id string, conn *JSONUnix, port int, privateData interface{}) *Device {
	// acquire locks in this order to avoid deadlock
	p.preDevicesMtx.Lock()
	defer p.preDevicesMtx.Unlock()
	p.devicesMtx.Lock()
	defer p.devicesMtx.Unlock()
	if _, ok := p.devices[id]; ok {
		// already registered
		return nil
	}
	d := newDevice(id, conn, port, privateData)
	if preDevice, ok := p.preDevices[id]; ok {
		d.Descriptor = preDevice.Desc
		delete(p.preDevices, id)
		preDevice.RegCh <- true
	}
	p.devices[id] = d

	return d
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

// List the registered groups' ids
func (p *DevicePool) GroupIds() []string {
	set := make(map[string]bool)
	for _, d := range p.GetDeviceDescList() {
		set[d.GroupName] = true
	}
	ret := make([]string, 0, len(set))
	for k := range set {
		ret = append(ret, k)
	}
	return ret
}

// List the registered devices' ids
func (p *DevicePool) DeviceIds() []string {
	p.devicesMtx.Lock()
	defer p.devicesMtx.Unlock()
	ret := make([]string, 0, len(p.devices))
	for key := range p.devices {
		ret = append(ret, key)
	}
	return ret
}

func (p *DevicePool) GetDeviceDescList() []*apiv1.DeviceDescriptor {
	p.devicesMtx.Lock()
	defer p.devicesMtx.Unlock()
	ret := make([]*apiv1.DeviceDescriptor, 0)
	for _, device := range p.devices {
		ret = append(ret, &device.Descriptor)
	}
	return ret
}

func (p *DevicePool) GetDeviceDescByGroupId(groupId string) []*apiv1.DeviceDescriptor {
	ret := make([]*apiv1.DeviceDescriptor, 0)
	devs := p.GetDeviceDescList()
	for _, d := range devs {
		if d.GroupName == groupId {
			ret = append(ret, d)
		}
	}
	return ret
}
