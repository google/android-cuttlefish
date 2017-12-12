cc_library(
    name = "ivserver_lib",
    srcs = [
        "hald_client.cc",
        "hald_client.h",
        "ivserver.cc",
        "ivserver.h",
        "options.cc",
        "options.h",
        "qemu_client.cc",
        "qemu_client.h",
        "vsocsharedmem.cc",
        "vsocsharedmem.h",
    ],
    hdrs = [
        "hald_client.h",
        "ivserver.h",
        "options.h",
        "qemu_client.h",
        "vsocsharedmem.h",
    ],
    copts = [
        "-I/usr/include/jsoncpp",
    ],
    visibility = [ "//visibility:public" ],
    deps = [
        "@cuttlefish//common/libs/fs",
        "@cuttlefish_kernel//:uapi",
        "@gflags_repo//:gflags",
        "@glog_repo//:glog",
    ],
)

cc_test(
    name = "hald_client_test",
    srcs = [
        "hald_client_test.cc",
        "vsocsharedmem_mock.h",
    ],
    copts = [
        "-I/usr/include/jsoncpp",
    ],
    linkopts = [
        "-ljsoncpp",
    ],
    deps = [
        ":ivserver_lib",
        "@cuttlefish//common/libs/fs",
        "@glog_repo//:glog",
        "@gtest_repo//:gtest_main",
    ],
)
