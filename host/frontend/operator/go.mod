module cuttlefish/operator

go 1.19

replace cuttlefish/liboperator v0.0.0-unpublished => ../liboperator

require (
	cuttlefish/liboperator v0.0.0-unpublished
	github.com/gorilla/mux v1.8.0
)

require github.com/gorilla/websocket v1.4.2 // indirect
