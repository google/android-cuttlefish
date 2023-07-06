module github.com/google/android-cuttlefish/frontend/src/operator

go 1.21

replace github.com/google/android-cuttlefish/frontend/src/liboperator/proto v0.0.0-unpublished => ../liboperator/operator/intermediate/proto

replace github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished => ../liboperator

require github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished

require (
	github.com/golang/protobuf v1.5.3 // indirect
	github.com/google/android-cuttlefish/frontend/src/liboperator/proto v0.0.0-unpublished // indirect
	github.com/gorilla/mux v1.8.0 // indirect
	golang.org/x/net v0.12.0 // indirect
	golang.org/x/sys v0.10.0 // indirect
	golang.org/x/text v0.11.0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20230629202037-9506855d4529 // indirect
	google.golang.org/grpc v1.56.1 // indirect
	google.golang.org/protobuf v1.31.0 // indirect
)
