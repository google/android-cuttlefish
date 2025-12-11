load("//cuttlefish/bazel:rules.bzl", "cf_cc_binary")

package(
    default_visibility = ["//:android_cuttlefish"],
)

cf_cc_binary(
    name = "libmem_overrides.so",
    srcs = select({
        "@platforms//cpu:arm64": [
            "mem_overrides.cpp",
        ],
        "//conditions:default": [
            "mem_overrides_noop.cpp",
        ],
    }),
    copts = [
        "-fPIC",
    ],
    linkopts = [
        "-Wl,-no-undefined",
    ],
    linkshared = 1,
    deps = select({
        "@platforms//cpu:arm64": [
            "@arm_optimized_routines//:string",
        ],
        "//conditions:default": [],
    }),
)
