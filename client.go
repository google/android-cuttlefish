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
	"sync"

	"github.com/pion/webrtc/v3"
)

type Signaling struct {
	SendCh chan any
	RecvCh chan map[string]any
	// The ICE servers to use in the webRTC connection.
	ICEServers []webrtc.ICEServer
	// The servers that were created client side and need to be sent to the device.
	// This is typically a subset of Servers. Ignored if empty.
	ClientICEServers []webrtc.ICEServer
}

type Controller struct {
	signaling      *Signaling
	peerConnection *webrtc.PeerConnection
	observer       Observer
	sendMtx        sync.Mutex
	sendChClosed   bool
}

func (c *Controller) connect(onComplete func(error)) {
	c.peerConnection.OnConnectionStateChange(func(s webrtc.PeerConnectionState) {
		switch s {
		case webrtc.PeerConnectionStateConnected:
			// Connected successfully
			onComplete(nil)
		case webrtc.PeerConnectionStateFailed:
			onComplete(fmt.Errorf("Peer Connection failed"))
			c.observer.OnFailure()
			c.stopGoRoutines()
		case webrtc.PeerConnectionStateClosed:
			onComplete(fmt.Errorf("Peer Connection closed unexpectedly"))
			c.observer.OnClose()
			c.stopGoRoutines()
		default:
			// These are temporary states, ignore.
		}
	})

	c.peerConnection.OnICECandidate(func(candidate *webrtc.ICECandidate) {
		if candidate == nil {
			return
		}
		c.sendICECandidate(candidate.ToJSON())
	})

	// Handle signaling messages in a new go routine so this one can return when
	// the connection is established.
	go func() {
		err := c.recvLoop()
		if err != nil {
			c.observer.OnError(err)
		}
	}()

	// Request offer from the device to start the signaling process.
	c.sendSigMsg(NewRequestOfferMsg(c.signaling.ClientICEServers))
}

func (c *Controller) recvLoop() error {
	for {
		msg, open := <-c.signaling.RecvCh
		if !open {
			// Normally the recv channel is closed as a result of the send channel
			// closing, but that's not necessarily the case all the time.
			c.closeSendCh()
			return nil
		}
		switch msg["type"] {
		case RequestOfferMsgType:
			panic("Device requested an offer. This violates the signaling protocol")
		case OfferMsgType:
			offer, err := Reshape[webrtc.SessionDescription](msg)
			if err != nil {
				// msg was obtained from a JSON string so Reshape shouldn't fail
				panic(fmt.Sprintf("Failed to reshape json: %v", err))
			}
			if err := c.onOffer(offer); err != nil {
				return fmt.Errorf("Error handling offer: %w", err)
			}
		case ICECandidateMsgType:
			candidate, err := Reshape[webrtc.ICECandidateInit](msg)
			if err != nil {
				// msg was obtained from a JSON string so Reshape shouldn't fail
				panic(fmt.Sprintf("Failed to reshape json: %v", err))
			}
			if err := c.onICECandidate(candidate); err != nil {
				return fmt.Errorf("Error handling ICE candidate: %w", err)
			}
		case AnswerMsgType:
			answer, err := Reshape[webrtc.SessionDescription](msg)
			if err != nil {
				// msg was obtained from a JSON string so Reshape shouldn't fail
				panic(fmt.Sprintf("Failed to reshape json: %v", err))
			}
			if err := c.onAnswer(answer); err != nil {
				return fmt.Errorf("Error handling answer: %w", err)
			}
		default:
			if errMsg, errPresent := msg["error"]; errPresent {
				return fmt.Errorf("Received error from signaling server: %v", errMsg)
			} else {
				return fmt.Errorf("Unknown message type: %v", msg["type"])
			}
		}
	}
}

func (c *Controller) stopGoRoutines() {
	// When the peer connection closes the signaling goroutines must be stopped.
	// This process is triggered by closing the send channel which will notify
	// the forwarding signaling goroutine to exit since it won't have nothing
	// else to forward. This will eventually lead to the recv channel to close,
	// which will finally cause the go routine running the recv loop to exit and
	// complete the shutdown mechanism.
	c.closeSendCh()
}

func (c *Controller) closeSendCh() {
	c.sendMtx.Lock()
	defer c.sendMtx.Unlock()
	if !c.sendChClosed {
		close(c.signaling.SendCh)
		c.sendChClosed = true
	}
}

func (c *Controller) sendSigMsg(msg any) {
	c.sendMtx.Lock()
	defer c.sendMtx.Unlock()
	if c.sendChClosed {
		return
	}
	c.signaling.SendCh <- msg
}

func (c *Controller) onOffer(offer *webrtc.SessionDescription) error {
	if err := c.peerConnection.SetRemoteDescription(*offer); err != nil {
		return fmt.Errorf("Failed to set remote description: %w", err)
	}
	answer, err := c.peerConnection.CreateAnswer(nil)
	if err != nil {
		return fmt.Errorf("Failed to create answer: %w", err)
	}
	if err = c.peerConnection.SetLocalDescription(answer); err != nil {
		return fmt.Errorf("Failed to set local description: %w", err)
	}
	c.sendSigMsg(answer)
	return nil
}

func (c *Controller) onAnswer(answer *webrtc.SessionDescription) error {
	err := c.peerConnection.SetRemoteDescription(*answer)
	if err != nil {
		return fmt.Errorf("Failed to set remote description: %w", err)
	}
	return nil
}

func (c *Controller) onICECandidate(candidate *webrtc.ICECandidateInit) error {
	err := c.peerConnection.AddICECandidate(*candidate)
	if err != nil {
		return fmt.Errorf("Failed to add ICE candidate: %w", err)
	}
	return nil
}

func (c *Controller) sendICECandidate(candidate webrtc.ICECandidateInit) {
	c.sendSigMsg(NewICECandidateMsg(candidate))
}

type Observer interface {
	// Called when adb data channel is added to the peer connection
	OnADBDataChannel(*webrtc.DataChannel)
	// Called on signaling error
	OnError(error)
	// Called on peer connection failure
	OnFailure()
	// Called when the peer connection closes
	OnClose()
}

type Connection struct {
	controller Controller
}

// Connects to a device. Blocks until the connection is established successfully
// or fails. If the returned error is not nil the Connection should be
// ignored.
func NewConnection(signaling *Signaling, observer Observer) (*Connection, error) {
	cfg := webrtc.Configuration{
		SDPSemantics: webrtc.SDPSemanticsUnifiedPlanWithFallback,
		ICEServers:   signaling.ICEServers,
	}
	pc, err := webrtc.NewPeerConnection(cfg)
	if err != nil {
		return nil, fmt.Errorf("Failed to create peer connection: %w", err)
	}
	adbChannel, err := pc.CreateDataChannel("adb-channel", nil /*options*/)
	if err != nil {
		return nil, fmt.Errorf("Failed to create adb data channel: %w", err)
	}
	pc.OnNegotiationNeeded(func() {
		// TODO(jemoreira): This needs to be handled when unnecessary tracks and
		// channels are removed from the peer connection.
	})
	pc.OnTrack(func(tr *webrtc.TrackRemote, rtpRec *webrtc.RTPReceiver) {
		// TODO(jemoreira): Remove from the peer connection to save bandwidth.
	})

	observer.OnADBDataChannel(adbChannel)

	ret := &Connection{
		controller: Controller{
			signaling:      signaling,
			peerConnection: pc,
			observer:       observer,
		},
	}

	ch := make(chan error)
	// The channel will be read from only once, so make sure the other side only
	// writes once too.
	var once sync.Once
	ret.controller.connect(func(err error) {
		once.Do(func() {
			ch <- err
		})
	})
	err = <-ch
	return ret, err
}

func (dc Connection) Close() {
	dc.controller.peerConnection.Close()
}
