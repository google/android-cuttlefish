module github.com/google/android-cuttlefish/frontend/src/host_orchestrator

go 1.25.0

require (
	github.com/google/btree v1.1.3
	github.com/google/go-cmp v0.7.0
	github.com/google/uuid v1.6.0
	github.com/gorilla/mux v1.8.1
)

require (
	github.com/gorilla/websocket v1.5.3 // indirect
	golang.org/x/net v0.58.0 // indirect
	golang.org/x/sys v0.47.0 // indirect
	golang.org/x/text v0.41.0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20260706201446-f0a921348800 // indirect
	google.golang.org/grpc v1.85.0-dev // indirect
	google.golang.org/protobuf v1.36.12 // indirect
)

replace github.com/google/android-cuttlefish/frontend/src/liboperator => ../liboperator
