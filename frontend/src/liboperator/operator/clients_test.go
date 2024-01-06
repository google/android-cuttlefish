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
	"testing"
)

func TestNewPolledConnection(t *testing.T) {
	ps := NewPolledSet()
	c1 := ps.NewConnection(newDevice("d1", nil, 0, ""))
	if c1 == nil {
		t.Error("Failed to create polled connection")
	}
	c2 := ps.NewConnection(newDevice("d1", nil, 0, ""))
	if c2 == nil {
		t.Error("Failed to create polled connection")
	}
	if c1.Id() == c2.Id() {
		t.Error("Polled connections have the same id")
	}
}

func TestGetConnection(t *testing.T) {
	ps := NewPolledSet()
	c1 := ps.NewConnection(newDevice("d1", nil, 0, ""))
	if ps.GetConnection(c1.Id()) != c1 {
		t.Error("Failed to get connection by id")
	}
	if ps.GetConnection(c1.Id()+"some suffix") == c1 {
		t.Error("Returned connection with wrong id")
	}
	c2 := ps.NewConnection(newDevice("d1", nil, 0, ""))
	if ps.GetConnection(c1.Id()) == c2 || ps.GetConnection(c2.Id()) == c1 {
		t.Error("Returned the wrong connection")
	}
	ps.Destroy(c2.Id())
	if ps.GetConnection(c2.Id()) != nil {
		t.Error("Returned unregistered connection")
	}
	// should have no effect
	ps.Destroy(c2.Id())
	if ps.GetConnection(c1.Id()) != c1 {
		t.Error("Failed to get connection after destruction of another")
	}
}

func TestMessages(t *testing.T) {
	ps := NewPolledSet()
	c := ps.NewConnection(newDevice("d1", nil, 0, ""))
	msgs := c.GetMessages(0, -1)
	if len(msgs) != 0 {
		t.Error("Returned messages")
	}
	c.Send("msg1")
	c.Send("msg2")
	c.Send("msg3")
	msgs = c.GetMessages(0, -1)
	if len(msgs) != 3 || msgs[0] != "msg1" || msgs[2] != "msg3" {
		t.Error("Failed to return all messages: ", msgs)
	}
	msgs = c.GetMessages(1, 1)
	if len(msgs) != 1 && msgs[0] != "msg2" {
		t.Error("Failed to return message range: ", msgs)
	}
}

func TestDestroy(t *testing.T) {
	ps := NewPolledSet()
	c := ps.NewConnection(newDevice("d1", nil, 0, ""))
	c.OnDeviceDisconnected()
	if ps.GetConnection(c.Id()) != nil {
		t.Error("connection was not destroyed after device disconnect")
	}
}

func TestRandStr(t *testing.T) {
	r := rand.New(rand.NewSource(1))
	s1 := randStr(1, r)
	if len(s1) != 1 {
		t.Error("Returned string of wrong length")
	}
	s1 = randStr(100, r)
	s2 := randStr(100, r)
	if s1 == s2 {
		// The probability of this happening without a bug is 1 in 62^100
		t.Error("randStr returned two equal strings of len 100")
	}
}
