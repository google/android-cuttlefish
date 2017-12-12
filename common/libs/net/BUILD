cc_library(
    name = "net",
    srcs = [
        "netlink_client.cpp",
        "network_interface_manager.cpp",
        "netlink_client.h",
        "network_interface.h",
        "network_interface_manager.h",
    ],
    hdrs = [
        "netlink_client.h",
        "network_interface.h",
        "network_interface_manager.h",
    ],
    deps = [
        "//common/libs/fs"
    ],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "netlink_client_test",
    size = "small",
    srcs = ["netlink_client_test.cpp"],
    deps = [
        ":net",
        "//common/libs/glog:cuttlefish_logging",
        "@gtest_repo//:gtest_main",
    ],
)

