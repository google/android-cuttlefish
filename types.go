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
	"encoding/json"
	"fmt"

	"github.com/pion/webrtc/v3"
)

const (
	RequestOfferMsgType string = "request-offer"
	ICECandidateMsgType string = "ice-candidate"
	OfferMsgType        string = "offer"
	AnswerMsgType       string = "answer"
)

type RequestOfferMsg struct {
	Type       string             `json:"type"`
	ICEServers []webrtc.ICEServer `json:"ice_servers,omitempty`
}

func NewRequestOfferMsg(servers []webrtc.ICEServer) *RequestOfferMsg {
	return &RequestOfferMsg{
		Type:       RequestOfferMsgType,
		ICEServers: servers,
	}
}

type ICECandidateMsg struct {
	Type      string                  `json:"type"`
	Candidate webrtc.ICECandidateInit `json:"candidate"`
}

func NewICECandidateMsg(candidate webrtc.ICECandidateInit) *ICECandidateMsg {
	return &ICECandidateMsg{
		ICECandidateMsgType,
		candidate,
	}
}

// The offer and answer messages have the same structure as the
// webrtc.SessionDescription so they don't to be redefined here.

// Allows converting a generic JSON object representation, like
// map[string]interface{}, to a specific class such as ICECandidateMsg.
func Reshape[K interface{}](original interface{}) (*K, error) {
	str, err := json.Marshal(original)
	if err != nil {
		return nil, fmt.Errorf("Failed to marshal original message: %v", err)
	}
	var ret K
	err = json.Unmarshal(str, &ret)
	if err != nil {
		return nil, fmt.Errorf("Failed to unmarshal into new object: %v", err)
	}
	return &ret, nil
}
