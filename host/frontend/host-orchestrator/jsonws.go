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
	"log"
	"net/http"
	"sync"

	"github.com/gorilla/websocket"
)

// A websocket connection that can send and receive json objects.
// Using websockets requires some boilerplate code, even using gorilla, which is
// encapsulated behind this object.
// Only one thread should call Recv() at a time, Send() and Close() are thread safe
type JsonWs struct {
	conn     *websocket.Conn
	writeMtx sync.Mutex
}

var upgrader = websocket.Upgrader{} // default options

func NewJsonWs(w http.ResponseWriter, r *http.Request) *JsonWs {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println(err)
		return nil
	}
	return &JsonWs{conn: conn}
}

func (ws *JsonWs) Send(val interface{}) bool {
	msg, err := json.Marshal(val)
	if err != nil {
		log.Println(err)
		return false
	}

	ws.writeMtx.Lock()
	defer ws.writeMtx.Unlock()

	err = ws.conn.WriteMessage(websocket.TextMessage, msg)
	if err != nil {
		return false
	}

	return true
}

func (ws *JsonWs) Recv(val interface{}) bool {
	_, msg, err := ws.conn.ReadMessage()
	if err != nil {
		if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
			log.Printf("error: %v", err)
		}
		return false
	}
	if err := json.Unmarshal(msg, &val); err != nil {
		log.Println(err)
		return false
	}
	return true
}

func (ws *JsonWs) Close() {
	ws.writeMtx.Lock()
	defer ws.writeMtx.Unlock()
	ws.conn.WriteMessage(websocket.CloseMessage, []byte{})
	ws.conn.Close()
}
