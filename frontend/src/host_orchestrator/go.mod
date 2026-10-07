module github.com/google/android-cuttlefish/frontend/src/host_orchestrator

go 1.23.0

require (
	github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-20240822182916-7bea0dafdbde
	github.com/google/btree v1.1.3
	github.com/google/go-cmp v0.5.9
	github.com/google/uuid v1.6.0
	github.com/gorilla/mux v1.8.0
)

require (
	github.com/golang/protobuf v1.5.3 // indirect
	github.com/gorilla/websocket v1.5.3 // indirect
	golang.org/x/net v0.38.0 // indirect
	golang.org/x/sys v0.31.0 // indirect
	golang.org/x/text v0.23.0 // indirect
	google.golang.org/genproto v0.0.0-20230410155749-daa745c078e1 // indirect
	google.golang.org/grpc v1.56.3 // indirect
	google.golang.org/protobuf v1.33.0 // indirect
)

replace github.com/google/android-cuttlefish/frontend/src/liboperator => ../liboperator
