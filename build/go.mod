module android/soong/cuttlefish

require (
	android/soong v0.0.0
	github.com/google/blueprint v0.0.0
)

replace github.com/google/blueprint v0.0.0 => ../../../../build/blueprint

replace android/soong v0.0.0 => ../../../../build/soong

go 1.23

toolchain go1.24.1
