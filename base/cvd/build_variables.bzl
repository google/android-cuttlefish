"""
Compiler options applied by default to cf_cc_* targets.
"""

COPTS = [
    "-std=c++20",
    # https://cs.android.com/android/platform/superproject/main/+/main:build/soong/cc/config/global.go;l=428;drc=27f57506c28cc8e4f6a0c0b8ac22c85aa9140688
    "-ftrivial-auto-var-init=zero",
    "-DNODISCARD_EXPECTED",
    "-DGRPC_CHOOSE_OSS_BUILD",
    # Annotate std::mutex and friends so thread-safety analysis covers them.
    "-D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS",
    "-Werror=thread-safety-analysis",
]

LINKOPTS = [
]
