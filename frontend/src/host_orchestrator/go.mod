module github.com/google/android-cuttlefish/frontend/src/host_orchestrator

go 1.26.0

require (
	github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-00010101000000-000000000000
	github.com/google/btree v1.1.3
	github.com/google/go-cmp v0.7.0
	github.com/google/uuid v1.6.0
	github.com/gorilla/mux v1.8.1
)

require (
	github.com/gorilla/websocket v1.5.3 // indirect
	golang.org/x/net v0.63.0 // indirect
	golang.org/x/sys v0.50.0 // indirect
	golang.org/x/text v0.44.0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20261118221215-c8830f628b3f // indirect
	google.golang.org/grpc v1.85.0 // indirect
	google.golang.org/protobuf v1.36.12 // indirect
)

replace github.com/google/android-cuttlefish/frontend/src/liboperator => ../liboperator
