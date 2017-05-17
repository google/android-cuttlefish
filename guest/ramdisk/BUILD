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
        "//common/auto_resources",
    ],
    linkopts = [
        "-lz",
    ],
    visibility = [ "//visibility:public" ],
)
