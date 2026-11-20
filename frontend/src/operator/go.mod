module github.com/google/android-cuttlefish/frontend/src/operator

go 1.25.0

replace github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished => ../liboperator

require github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished

require (
	github.com/gorilla/mux v1.8.1 // indirect
	github.com/gorilla/websocket v1.5.3 // indirect
	golang.org/x/net v0.58.0 // indirect
	golang.org/x/sys v0.47.0 // indirect
	golang.org/x/text v0.41.0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20260706201446-f0a921348800 // indirect
	google.golang.org/grpc v1.85.0-dev // indirect
	google.golang.org/protobuf v1.36.12 // indirect
)
