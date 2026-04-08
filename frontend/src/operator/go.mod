module github.com/google/android-cuttlefish/frontend/src/operator

go 1.24.0

replace github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished => ../liboperator

require github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished

require (
	github.com/golang/protobuf v1.5.4 // indirect
	github.com/gorilla/mux v1.8.0 // indirect
	github.com/gorilla/websocket v1.5.3 // indirect
	golang.org/x/net v0.48.0 // indirect
	golang.org/x/sys v0.39.0 // indirect
	golang.org/x/text v0.32.0 // indirect
	google.golang.org/genproto v0.0.0-20200526211855-cb27e3aa2013 // indirect
	google.golang.org/grpc v1.79.3 // indirect
	google.golang.org/protobuf v1.36.10 // indirect
)
