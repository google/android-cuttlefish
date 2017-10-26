cc_library(
    name = "net",
    srcs = [
        "netlink_client.cpp",
        "netlink_client.h",
        "netlink_request.cpp",
        "netlink_request.h",
        "network_interface.h",
        "network_interface_manager.cpp",
        "network_interface_manager.h",
    ],
    hdrs = [
        "netlink_client.h",
        "network_interface.h",
        "network_interface_manager.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//common/libs/fs",
    ],
)

cc_test(
    name = "netlink_request_test",
    size = "small",
    srcs = ["netlink_request_test.cpp"],
    deps = [
        ":net",
        "//common/libs/glog:cuttlefish_logging",
        "@gtest_repo//:gtest_main",
    ],
)
