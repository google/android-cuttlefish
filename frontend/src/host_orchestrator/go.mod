module github.com/google/android-cuttlefish/frontend/src/host_orchestrator

go 1.25.0

require (
	github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-20240822182916-7bea0dafdbde
	github.com/google/btree v1.1.3
	github.com/google/go-cmp v0.7.0
	github.com/google/uuid v1.6.0
	github.com/gorilla/mux v1.8.0
)

require (
	github.com/golang/protobuf v1.5.4 // indirect
	github.com/gorilla/websocket v1.5.3 // indirect
	golang.org/x/net v0.58.0 // indirect
	golang.org/x/sys v0.47.0 // indirect
	golang.org/x/text v0.41.0 // indirect
	google.golang.org/genproto v0.0.0-20200526211855-cb27e3aa2013 // indirect
	google.golang.org/grpc v1.83.2 // indirect
	google.golang.org/protobuf v1.36.11 // indirect
)

replace github.com/google/android-cuttlefish/frontend/src/liboperator => ../liboperator
