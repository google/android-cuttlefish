cc_library(
    name = "gce_network_lib",
    visibility = ["//visibility:public"],
    srcs = [
        "dhcp_message.cpp",
        "dhcp_server.cpp",
        "metadata_proxy.cpp",
        "namespace_aware_executor.cpp",
        "netlink_client.cpp",
        "network_interface_manager.cpp",
        "network_namespace_manager.cpp",
        "serializable.cpp",
        "sys_client.cpp",
    ],
    hdrs = [
        "dhcp_message.h",
        "dhcp_server.h",
        "metadata_proxy.h",
        "namespace_aware_executor.h",
        "netlink_client.h",
        "network_interface.h",
        "network_interface_manager.h",
        "network_namespace_manager.h",
        "serializable.h",
        "sys_client.h",
    ],
    copts = [],
    linkopts = select({
        "//:link_static": [
            "-static",
            "-static-libgcc",
        ],
        "//conditions:default": [
        ],
    }),
    deps = [
        "@cuttlefish//common/libs/auto_resources",
        "@cuttlefish//common/libs/fs",
        "@cuttlefish//common/libs/metadata:constants",
        "@cuttlefish//common/libs/threads",
        "@glog_repo//:glog",
    ],
)

cc_binary(
    name = "meta_test",
    visibility = ["//visibility:public"],
    srcs = [
        "test.cpp",
    ],
    deps = [
        "@cuttlefish//common/libs/fs",
        "@glog_repo//:glog",
    ],
    linkstatic = 1,
    linkopts = [
        "-static",
        "-static-libgcc",
    ],
)


cc_test(
    name = "netlink_client_test",
    srcs = [
        "netlink_client_test.cpp",
        "sys_client_mock.h",
    ],
    deps = [
        ":gce_network_lib",
        "@gtest_repo//:gtest_main",
    ],
)

