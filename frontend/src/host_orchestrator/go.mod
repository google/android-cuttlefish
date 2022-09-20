module github.com/google/android-cuttlefish/frontend/src/host_orchestrator

go 1.19

replace github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished => ../liboperator

require (
	github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished
	github.com/google/uuid v1.3.0
	github.com/gorilla/mux v1.8.0
)
