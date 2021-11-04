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
	"time"

	"github.com/gorilla/websocket"
)

// A websocket connection that can send and receive json objects.
// Using websockets requires some boilerplate code, even using gorilla, which is
// encapsulated behind this object.
// All methods in this struct are thread safe.
type JsonWs struct {
	send chan []byte
	recv chan []byte
	cl   chan bool
	conn *websocket.Conn
}

func NewJsonWs(w http.ResponseWriter, r *http.Request) *JsonWs {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println(err)
		return nil
	}
	send, recv, cl := make(chan []byte, 100), make(chan []byte, 100), make(chan bool)
	go writePump(conn, send)
	go readPump(conn, recv, cl)
	return &JsonWs{send: send, recv: recv, cl: cl, conn: conn}
}

func (ws *JsonWs) Send(val interface{}) bool {
	select {
	case _, _ = <-ws.cl:
		// Activity on this channel means the ws is closed
		return false
	default:
		// Do nothing
	}
	msg, err := json.Marshal(val)
	if err != nil {
		log.Println(err)
		return false
	}
	ws.send <- msg
	return true
}

func (ws *JsonWs) Recv(val interface{}) bool {
	msg, ok := <-ws.recv
	if !ok {
		return false
	}
	if err := json.Unmarshal(msg, &val); err != nil {
		log.Println(err)
		return false
	}

	return true
}

func (ws *JsonWs) Close() {
	close(ws.send)
}

const (
	// Time allowed to write a message to the peer.
	writeWait = 10 * time.Second

	// Time allowed to read the next pong message from the peer.
	pongWait = 60 * time.Second

	// Send pings to peer with this period. Must be less than pongWait.
	pingPeriod = (pongWait * 9) / 10

	// Maximum message size allowed from peer.
	maxMessageSize = 10240
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  20480,
	WriteBufferSize: 20480,
}

func readPump(conn *websocket.Conn, ch chan []byte, cl chan bool) {
	defer func() {
		close(ch)
		close(cl)
	}()
	conn.SetReadLimit(maxMessageSize)
	conn.SetReadDeadline(time.Now().Add(pongWait))
	conn.SetPongHandler(func(string) error { conn.SetReadDeadline(time.Now().Add(pongWait)); return nil })
	for {
		_, msg, err := conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("error: %v", err)
			}
			break
		}
		ch <- msg
	}
}

func writePump(conn *websocket.Conn, ch chan []byte) {
	ticker := time.NewTicker(pingPeriod)
	defer ticker.Stop()
	for {
		select {
		case msg, ok := <-ch:
			conn.SetWriteDeadline(time.Now().Add(writeWait))
			if !ok {
				conn.WriteMessage(websocket.CloseMessage, []byte{})
				conn.Close()
				return
			}
			w, err := conn.NextWriter(websocket.TextMessage)
			if err != nil {
				conn.Close()
				// The writer won't return until the send channel is
				// closed to avoid blocking an application thread
				continue
			}
			w.Write(msg)

			if err := w.Close(); err != nil {
				conn.Close()
				// The writer won't return until the send channel is
				// closed to avoid blocking an application thread
				continue
			}
		case <-ticker.C:
			conn.SetWriteDeadline(time.Now().Add(writeWait))
			if err := conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				conn.Close()
				// The writer won't return until the send channel is
				// closed to avoid blocking an application thread
				continue
			}
		}
	}
}
