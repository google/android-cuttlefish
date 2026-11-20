module github.com/google/android-cuttlefish/frontend/src/operator

go 1.26.0

replace github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished => ../liboperator

require github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished

require (
	github.com/gorilla/mux v1.8.1 // indirect
	github.com/gorilla/websocket v1.5.3 // indirect
	golang.org/x/net v0.63.0 // indirect
	golang.org/x/sys v0.50.0 // indirect
	golang.org/x/text v0.44.0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20261118221215-c8830f628b3f // indirect
	google.golang.org/grpc v1.85.0 // indirect
	google.golang.org/protobuf v1.36.12 // indirect
)
