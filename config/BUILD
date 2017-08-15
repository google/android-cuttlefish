cc_library(
    name = "config_lib",
    srcs = [
        "file_partition.cpp",
        "file_partition.h",
        "guest_config.cpp",
        "guest_config.h",
    ],
    hdrs = [
        "guest_config.h",
    ],
    copts = [
        "-I/usr/include/libxml2",
    ],
    deps = [
        "@cuttlefish//common/libs/fs",
        "@glog_repo//:glog",
    ],
    visibility = [ "//visibility:public" ],
)

