module github.com/google/android-cuttlefish/e2etests

go 1.25.0

require (
	github.com/bazelbuild/rules_go v0.64.2
	github.com/google/android-cuttlefish/frontend/src/host_orchestrator v0.0.0-00010101000000-000000000000
	github.com/google/android-cuttlefish/frontend/src/libhoclient v0.0.0-00010101000000-000000000000
	github.com/google/go-cmp v0.7.0
)

require (
	github.com/cenkalti/backoff/v4 v4.3.0 // indirect
	github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-00010101000000-000000000000 // indirect
	github.com/google/uuid v1.6.0 // indirect
	github.com/gorilla/websocket v1.5.3 // indirect
	github.com/pion/datachannel v1.6.4 // indirect
	github.com/pion/dtls/v3 v3.1.16 // indirect
	github.com/pion/ice/v4 v4.4.9 // indirect
	github.com/pion/interceptor v0.1.51 // indirect
	github.com/pion/logging v0.2.4 // indirect
	github.com/pion/mdns/v2 v2.2.2 // indirect
	github.com/pion/randutil v0.1.0 // indirect
	github.com/pion/rtcp v1.2.20 // indirect
	github.com/pion/rtp v1.10.6 // indirect
	github.com/pion/sctp v1.12.1 // indirect
	github.com/pion/sdp/v3 v3.0.20 // indirect
	github.com/pion/srtp/v3 v3.1.3 // indirect
	github.com/pion/stun/v4 v4.0.1 // indirect
	github.com/pion/transport/v5 v5.1.5 // indirect
	github.com/pion/turn/v5 v5.1.3 // indirect
	github.com/pion/webrtc/v4 v4.2.27 // indirect
	github.com/wlynxg/anet v0.0.5 // indirect
	golang.org/x/crypto v0.55.0 // indirect
	golang.org/x/net v0.58.0 // indirect
	golang.org/x/sys v0.47.0 // indirect
	golang.org/x/time v0.15.0 // indirect
)

replace github.com/google/android-cuttlefish/frontend/src/libhoclient => ../frontend/src/libhoclient

replace github.com/google/android-cuttlefish/frontend/src/liboperator => ../frontend/src/liboperator

replace github.com/google/android-cuttlefish/frontend/src/host_orchestrator => ../frontend/src/host_orchestrator
