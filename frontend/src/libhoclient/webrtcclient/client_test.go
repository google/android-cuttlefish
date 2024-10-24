// Copyright 2022 Google LLC
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

package webrtcclient

import (
	"fmt"
	"testing"
	"time"

	"github.com/pion/webrtc/v3"
)

// An example offer sdp string with a single data channel
const fakeSDP string = `v=0
o=- 7723759704302471067 2 IN IP4 127.0.0.1
s=-
t=0 0
a=group:BUNDLE 0
a=extmap-allow-mixed
a=msid-semantic: WMS
m=application 9 UDP/DTLS/SCTP webrtc-datachannel
c=IN IP4 0.0.0.0
a=ice-ufrag:yS7P
a=ice-pwd:DML7anbSv0d83sY1TspIUSAM
a=ice-options:trickle
a=fingerprint:sha-256 AC:2D:41:40:67:D7:AB:B8:42:51:A6:E8:A3:2B:AE:26:24:76:FA:E6:8C:25:78:FF:2B:69:F3:16:DC:E3:BB:3A
a=setup:actpass
a=mid:0
a=sctp-port:5000
a=max-message-size:262144
`

type TestObserver struct {
	onADBDataChannel func(*webrtc.DataChannel)
	onError          func(error)
}

func (o *TestObserver) OnADBDataChannel(dc *webrtc.DataChannel) {
	if o.onADBDataChannel != nil {
		o.onADBDataChannel(dc)
	}
}

func (o *TestObserver) OnError(err error) {
	if o.onError != nil {
		o.onError(err)
	}
}

func (o *TestObserver) OnClose() {
	if o.onError != nil {
		o.onError(fmt.Errorf("connection closed"))
	}
}

func (o *TestObserver) OnFailure() {
	if o.onError != nil {
		o.onError(fmt.Errorf("connection failed"))
	}
}

func recvWithTimeOut[K any](sendCh chan any) (*K, error) {
	select {
	case m := <-sendCh:
		// Reshape instead of type check here: the client is allowed to send whatever
		// type it wants as long as has the expected fields.
		return Reshape[K](m)
	case <-time.After(1 * time.Second):
		return nil, fmt.Errorf("time out")
	}
}

func TestClientSendsInitialMessage(t *testing.T) {
	observer := TestObserver{}
	signaling := Signaling{
		SendCh: make(chan any),
		RecvCh: make(chan map[string]any),
	}

	// This won't return, so run it in another routine
	go func() {
		_, _ = NewConnection(&signaling, &observer)
	}()

	msg, err := recvWithTimeOut[RequestOfferMsg](signaling.SendCh)
	if err != nil {
		t.Fatalf("Client didn't send the request-offer message in time: %v", err)
	}
	if msg.Type != RequestOfferMsgType {
		t.Fatalf("First message has wrong type: '%v', expected '%s'", msg.Type, RequestOfferMsgType)
	}

}

func TestClientGeneratesAnswer(t *testing.T) {
	observer := TestObserver{}
	signaling := Signaling{
		SendCh: make(chan any),
		RecvCh: make(chan map[string]any),
	}

	// This won't return, so run it in another routine
	go func() {
		_, _ = NewConnection(&signaling, &observer)
	}()

	// The first message is always the request-offer
	if _, err := recvWithTimeOut[RequestOfferMsg](signaling.SendCh); err != nil {
		t.Skipf("Client didn't send request-offer in time: %v", err)
	}
	signaling.RecvCh <- map[string]any{
		"type": "offer",
		"sdp":  fakeSDP,
	}
	msg, err := recvWithTimeOut[webrtc.SessionDescription](signaling.SendCh)
	if err != nil {
		t.Fatalf("Client didn't send the answer message in time: %v", err)
	}
	if msg.Type != webrtc.SDPTypeAnswer {
		t.Fatalf("Expected 'answer' message, but got '%v' instead", msg.Type)
	}
}
