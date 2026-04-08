module github.com/google/android-cuttlefish/frontend/src/host_orchestrator

go 1.24.0

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
	golang.org/x/net v0.48.0 // indirect
	golang.org/x/sys v0.39.0 // indirect
	golang.org/x/text v0.32.0 // indirect
	google.golang.org/genproto v0.0.0-20200526211855-cb27e3aa2013 // indirect
	google.golang.org/grpc v1.79.3 // indirect
	google.golang.org/protobuf v1.36.10 // indirect
)

replace github.com/google/android-cuttlefish/frontend/src/liboperator => ../liboperator
