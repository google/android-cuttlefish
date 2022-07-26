module cuttlefish/host-orchestrator

go 1.19

replace cuttlefish/liboperator v0.0.0-unpublished => ../liboperator

require (
	cuttlefish/liboperator v0.0.0-unpublished
	github.com/google/uuid v1.3.0
	github.com/gorilla/mux v1.8.0
)
