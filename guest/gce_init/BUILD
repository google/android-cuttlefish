cc_binary(
    name = "gce_init",
    visibility = ["//visibility:public"],
    srcs = [
        "environment_setup.cpp",
        "environment_setup.h",
        "gce_init.cpp",
        "properties.cpp",
        "properties.h",
    ],
    deps = [
        "@cuttlefish//common/libs/auto_resources",
        "@cuttlefish//common/libs/metadata:initial_metadata_reader-static",
        "@cuttlefish//common/libs/metadata:partitions",
        "//guest/gce_network:gce_network_lib",
        "//guest/ramdisk:ramdisk_lib",
    ],
    linkopts = [
        "-Wl,-Bstatic",
        "-static-libgcc",
    ],
    linkstatic = 1,
)

cc_binary(
    name = "gce_init_dhcp_hook",
    visibility = ["//visibility:public"],
    srcs = [
        "gce_init_dhcp_hook.cpp",
    ],
    deps = [
        "@cuttlefish//common/libs/auto_resources",
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
        "@cuttlefish//common/libs/auto_resources",
        "@gtest_repo//:gtest_main",
    ],
)
