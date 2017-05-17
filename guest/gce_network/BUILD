load("//common/config/BUILD.link_type");

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
        "logging.h",
        "metadata_proxy.h",
        "namespace_aware_executor.h",
        "netlink_client.h",
        "network_interface.h",
        "network_interface_manager.h",
        "network_namespace_manager.h",
        "serializable.h",
        "sys_client.h",
    ],
    copts = [
        "-I/usr/include/jsoncpp",
    ],
    linkopts = select({
        "//link_type:force_static": [
            "-static",
            "-static-libgcc",
            "-Wl,-Bstatic -ljsoncpp",
        ],
        "//conditions:default": [
            "-ljsoncpp",
            "-lcurl",
        ],
    }),
    deps = [
        "//common/auto_resources",
        "//common/fs",
        "//common/metadata:constants",
        "//common/threads",
        "@glog_repo//:glog",
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

