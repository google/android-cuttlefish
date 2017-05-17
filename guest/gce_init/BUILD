load("//common/config/link_type.bzl");

cc_binary(
    name = "gce_init",
    srcs = [
        "environment_setup.cpp",
        "environment_setup.h",
        "gce_init.cpp",
        "properties.cpp",
        "properties.h",
    ],
    deps = [
        "//common/auto_resources",
        "//common/metadata:initial_metadata_reader-static",
        "//common/metadata:partitions",
        "//guest/gce_network:gce_network_lib",
        "//guest/ramdisk:ramdisk_lib",
    ],
    linkopts = [
        "-Wl,-Bstatic",
    ],
    linkstatic = 1,
)

cc_test(
    name = "properties_test",
    srcs = [
        "properties.cpp",
        "properties.h",
        "properties_test.cpp",
    ],
    deps = [
        "//common/auto_resources",
        "@gtest_repo//:gtest_main",
    ],
)
