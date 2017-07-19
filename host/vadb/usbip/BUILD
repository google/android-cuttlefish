cc_library(
    name = "usbip_lib",
    srcs = [
        "client.cpp",
        "client.h",
        "device.h",
        "device_pool.cpp",
        "device_pool.h",
        "messages.cpp",
        "messages.h",
        "server.cpp",
        "server.h",
    ],
    hdrs = [
        "client.h",
        "device.h",
        "device_pool.h",
        "messages.h",
        "server.h",
    ],
    deps = [
        "@cuttlefish//common/libs/fs",
        "@gflags_repo//:gflags",
        "@glog_repo//:glog",
    ],
)

