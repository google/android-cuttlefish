module github.com/google/android-cuttlefish/frontend/src/operator

go 1.19

replace github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished => ../liboperator

require (
	github.com/google/android-cuttlefish/frontend/src/liboperator v0.0.0-unpublished
	github.com/gorilla/mux v1.8.0
)
