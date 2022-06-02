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
	"log"
	"net"
	"net/http"
	"sync"
	"syscall"

	"github.com/gorilla/websocket"
)

// A websocket connection that can send and receive JSON objects.
// Only one thread should call Recv() at a time, Send() and Close() are thread safe
type JSONWs struct {
	conn     *websocket.Conn
	writeMtx sync.Mutex
}

var upgrader = websocket.Upgrader{} // default options

func NewJSONWs(w http.ResponseWriter, r *http.Request) *JSONWs {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println(err)
		return nil
	}
	return &JSONWs{conn: conn}
}

func (ws *JSONWs) Send(val interface{}) error {
	ws.writeMtx.Lock()
	defer ws.writeMtx.Unlock()
	return ws.conn.WriteJSON(val)
}

func (ws *JSONWs) Recv(val interface{}) error {
	return ws.conn.ReadJSON(val)
}

func (ws *JSONWs) Close() {
	ws.writeMtx.Lock()
	defer ws.writeMtx.Unlock()
	ws.conn.WriteMessage(websocket.CloseMessage, []byte{})
	ws.conn.Close()
}

// A Unix socket connection (as returned by Accept) that can send recieve JSON objects.
// Only one thread should call Recv() at a time, Send and Close are thread safe
type JSONUnix struct {
	conn     *net.UnixConn
	raw      syscall.RawConn
	writeMtx sync.Mutex
	buff     []byte
}

func NewJSONUnix(c *net.UnixConn) *JSONUnix {
	raw, err := c.SyscallConn()
	if err != nil {
		log.Println("Failed to get raw connection from unix connection: ", err)
		return nil
	}
	return &JSONUnix{
		conn: c,
		raw:  raw,
		buff: make([]byte, 4096),
	}
}

func (c *JSONUnix) Send(val interface{}) error {
	s, err := json.Marshal(val)
	if err != nil {
		return err
	}
	c.writeMtx.Lock()
	defer c.writeMtx.Unlock()
	_, err = c.conn.Write(s)
	return err
}

func (c *JSONUnix) Recv(val interface{}) error {
	l, err := c.getNextMsgLen()
	if err != nil {
		return err
	}
	if l > len(c.buff) {
		// The smallest multiple of 4096 that'll fit the message
		l = (l + 4095) & ^4095
		c.buff = make([]byte, l)
	}
	n, err := c.conn.Read(c.buff)
	if err != nil {
		return err
	}
	return json.Unmarshal(c.buff[:n], val)
}

func (c *JSONUnix) Close() {
	c.writeMtx.Lock()
	defer c.writeMtx.Unlock()
	c.conn.Close()
}

func (c *JSONUnix) getNextMsgLen() (int, error) {
	var n int
	var errInner error
	err := c.raw.Read(func(fd uintptr) bool {
		buff := make([]byte, 0)
		n, _, errInner = syscall.Recvfrom(int(fd), buff, syscall.MSG_TRUNC|syscall.MSG_PEEK)
		return errInner != syscall.EAGAIN
	})
	if err != nil {
		return -1, err
	}
	return n, errInner
}
