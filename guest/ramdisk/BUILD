cc_library(
    name = "ramdisk_lib",
    srcs = [
        "compressed_file_reader.cpp",
        "compressed_file_reader.h",
        "unpack_ramdisk.cpp",
        "unpack_ramdisk.h",
    ],
    hdrs = [
        "unpack_ramdisk.h",
    ],
    deps = [
        "@cuttlefish//common/libs/auto_resources",
        "@glog_repo//:glog",
    ],
    linkopts = [
        "-lz",
    ],
    visibility = [ "//visibility:public" ],
)
