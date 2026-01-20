COPTS = [
    "-std=c++20",
    # https://cs.android.com/android/platform/superproject/main/+/main:build/soong/cc/config/global.go;l=428;drc=27f57506c28cc8e4f6a0c0b8ac22c85aa9140688
    "-ftrivial-auto-var-init=zero",
    "-DNODISCARD_EXPECTED",
]
