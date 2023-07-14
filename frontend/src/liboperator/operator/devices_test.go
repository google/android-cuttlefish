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
	"testing"
)

func TestNewDevice(t *testing.T) {
	d := NewDevice(nil, 0, "info")
	if d == nil {
		t.Error("null device")
	}
}

type MockClient struct {
	sendCount       int
	disconnectCount int
}

func (c *MockClient) Send(msg interface{}) error {
	c.sendCount++
	return nil
}

func (c *MockClient) OnDeviceDisconnected() {
	c.disconnectCount++
}

func TestDeviceRegister(t *testing.T) {
	d := NewDevice(nil, 0, "info")
	// Unregistering unexisting client has no effect
	d.Unregister(9)
	// Register a client
	c1 := &MockClient{}
	c2 := &MockClient{}
	id1 := d.Register(c1)
	if id1 == -1 {
		t.Error("-1 client id")
	}
	id2 := d.Register(c2)
	if id2 == -1 {
		t.Error("-1 client id")
	}
	if id1 == id2 {
		t.Error("Same id for different clients")
	}
	if err := d.ToClient(id1, "msg"); err != nil {
		t.Error("Failed to send to registered device: ", err)
	}
	if err := d.ToClient(id2, "msg"); err != nil {
		t.Error("Failed to send to registered device: ", err)
	}
	if d.ToClient(-1, "msg") == nil {
		t.Error("Succeeded in sending to unexisting client")
	}

	d.Unregister(id1)
	d.DisconnectClients()

	if d.ToClient(id1, "msg") == nil {
		t.Error("Succeeded sending to unregistered client")
	}

	if c1.sendCount != 1 {
		t.Error("Did not send on c1")
	}
	if c2.sendCount != 1 {
		t.Error("Did not send on c2")
	}
	if c1.disconnectCount != 0 {
		t.Error("Disconnected on unregistered device")
	}
	if c2.disconnectCount != 1 {
		t.Error("Did not disconnect on registered device")
	}
}

func TestPoolRegister(t *testing.T) {
	p := NewDevicePool()
	// Unregistering wrong device has no effect
	p.Unregister("id1")
	d := NewDevice(nil, 0, "info1")
	if p.GetDevice("id1") != nil {
		t.Error("Empty pool returned device")
	}
	if !p.Register(d, "id1") {
		t.Error("Failed to register new device")
	}
	if p.GetDevice("id1") != d {
		t.Error("Failed to get registered device")
	}
	if p.GetDevice("id2") == d {
		t.Error("Returned wrong device")
	}
	// Register with the same id
	if p.Register(d, "id1") {
		t.Error("Didn't fail with same device id")
	}
	if !p.Register(d, "id2") {
		t.Error("Failed to register with different id")
	}
	// Try to get device after unregistering another
	p.Unregister("id2")
	if p.GetDevice("id1") != d {
		t.Error("Failed to get registered device")
	}
	p.Unregister("id1")
	if p.GetDevice("id1") != nil {
		t.Error("Returned unregistered device")
	}
}

func TestListDevices(t *testing.T) {
	p := NewDevicePool()
	d1 := NewDevice(nil, 0, "info1")
	d2 := NewDevice(nil, 0, "info2")
	if len(p.DeviceIds()) != 0 {
		t.Error("Empty pool listed devices")
	}
	p.Register(d1, "1")
	l := p.DeviceIds()
	if len(l) != 1 || l[0] != "1" {
		t.Error("Error listing after 1 device registration: ", l)
	}
	p.Register(d2, "2")
	l = p.DeviceIds()
	if len(l) != 2 || l[0] != "1" && l[0] != "2" || l[1] != "1" && l[1] != "2" || l[0] == l[1] {
		t.Error("Error listing after 2 device restrations: ", l)
	}
	p.Unregister("1")
	l = p.DeviceIds()
	if len(l) != 1 || l[0] != "2" {
		t.Error("Error listing after 2 device registrations and 1 unregistration: ", l)
	}
	// Should have no effect
	p.Unregister("1")
	if len(l) != 1 || l[0] != "2" {
		t.Error("Error listing after 2 device registrations, 1 unregistration and 1 failed unregistration: ", l)
	}
	p.Unregister("2")
	l = p.DeviceIds()
	if len(p.DeviceIds()) != 0 {
		t.Error("Empty pool listed devices")
	}
}

func MakeInfo(groupId string) map[string]interface{} {
	return map[string]interface{}{
		"group_id": groupId,
	}
}

func TestListDevicesByGroup(t *testing.T) {
	p := NewDevicePool()

	foo_d1 := NewDevice(nil, 0, MakeInfo("foo"))
	foo_d2 := NewDevice(nil, 0, MakeInfo("foo"))
	bar_d1 := NewDevice(nil, 0, MakeInfo("bar"))
	bar_d2 := NewDevice(nil, 0, MakeInfo("bar"))
	bar_d3 := NewDevice(nil, 0, MakeInfo("bar"))

	p.Register(foo_d1, "1")
	p.Register(foo_d2, "2")
	p.Register(bar_d1, "3")
	p.Register(bar_d2, "4")
	p.Register(bar_d3, "5")

	if deviceCnt := len(p.GetDeviceInfoListByGroupId("foo")); deviceCnt != 2 {
		t.Error("List of devices in group foo should have size of 2, but have ", deviceCnt)
	}

	if deviceCnt := len(p.GetDeviceInfoListByGroupId("bar")); deviceCnt != 3 {
		t.Error("List of devices in group bar should have size of 3, but have ", deviceCnt)
	}
}

func TestListDevicesEmpty(t *testing.T) {
	p := NewDevicePool()
	d := NewDevice(nil, 0, MakeInfo("foo"))
	p.Register(d, "d")
	p.Unregister("d")

	if deviceCnt := len(p.GetDeviceInfoList()); deviceCnt != 0 {
		t.Error("List of all devices should have size of 0, but have ", deviceCnt)
	}

	if deviceCnt := len(p.GetDeviceInfoListByGroupId("foo")); deviceCnt != 0 {
		t.Error("List of devices in group foo should have size of 0, but have ", deviceCnt)
	}
}

func TestGetGroupId(t *testing.T) {
	info := MakeInfo("foo")
	groupId := GetGroupId(info)

	if groupId != "foo" {
		t.Error("info should have group_id as foo")
	}

}

func TestListGroups(t *testing.T) {
	p := NewDevicePool()
	d1 := NewDevice(nil, 0, MakeInfo("group1"))
	d2 := NewDevice(nil, 0, MakeInfo("group2"))
	d3 := NewDevice(nil, 0, MakeInfo("group2"))
	if len(p.GroupIds()) != 0 {
		t.Error("Empty pool listed groups")
	}

	p.Register(d1, "d1")
	p.Register(d2, "d2")
	p.Register(d3, "d3")

	if len(p.devices) != 3 {
		t.Error("Error listing after 3 device registrations - expected 3 but ", len(p.devices))
	}

	if len(p.groups) != 2 {
		t.Error("Error listing after 3 device registrations - expected 2 but ", len(p.groups))
	}

	p.Unregister("d1")

	if len(p.groups) != 1 {
		t.Error("Error listing after 3 device registrations, 1 unregistration - expected 1 but ", len(p.groups))
	}

	p.Unregister(("d2"))
	if len(p.groups) != 1 {
		t.Error("Error listing after 3 device registrations, 2 unregistrations - expected 1 but ", len(p.groups))
	}
}

func TestDefaultGroup(t *testing.T) {
	p := NewDevicePool()
	d1 := NewDevice(nil, 0, MakeInfo("foo"))
	d2 := NewDevice(nil, 0, map[string]interface{}{})
	d3 := NewDevice(nil, 0, "")
	d4 := NewDevice(nil, 0, MakeInfo(""))

	p.Register(d1, "d1")
	p.Register(d2, "d2")
	p.Register(d3, "d3")
	p.Register(d4, "d4")

	if len(p.devices) != 4 {
		t.Error("Error listing after 4 device registrations - expected 4 but ", len(p.devices))
	}

	if defaultDeviceCount := len(p.groups[DEFAULT_GROUP_ID].deviceIds); defaultDeviceCount != 3 {
		t.Error("Error after 3 device in default group - expected 3 but ", defaultDeviceCount)
	}

	if defaultDeviceCount := len(p.GetDeviceInfoListByGroupId(DEFAULT_GROUP_ID)); defaultDeviceCount != 3 {
		t.Error("Error after 3 device in default group - expected 3 but ", defaultDeviceCount)
	}

}
