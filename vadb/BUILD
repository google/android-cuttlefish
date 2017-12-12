cc_binary(
    name = "vadb",
    srcs = [
        "main.cpp",
        "virtual_adb.cpp",
        "virtual_adb.h",
    ],
    deps = [
        "@cuttlefish//common/libs/fs",
        "@cuttlefish//guest/usbforward:protocol",
        "@gflags_repo//:gflags",
        "@glog_repo//:glog",
        "//host/vadb/usbip:usbip_lib",
    ],
)