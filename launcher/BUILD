load("//tools:py_bundle.bzl", "py_bundle")

py_library(
    name = "ivserver_lib",
    srcs = [
        "file_partition.py",
        "guest_definition.py",
        "ivserver.py",
        "libvirt_client.py",
    ],
    srcs_version = "PY2AND3",
)

py_bundle(
    name = "launcher",
    srcs = [":ivserver_lib"],
    main = "host.launcher.ivserver",
)

cc_library(
    name = "guest_lib",
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

