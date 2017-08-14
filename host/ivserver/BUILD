cc_binary(
    name = "ivserver",
    srcs = [
        "main.cc",
    ],
    copts = [
        "-I/usr/include/jsoncpp",
    ],
    linkopts = [
        # TODO(ender): find a better way to manage dependencies across
        # incompatible systems, like our obsolete Goobuntu vs modern Debian distros.
        # The following libraries must be linked statically
        # to avoid problems with missing (obsolete) library versions on newer systems.
        "-l:libjsoncpp.a",
        "-ludev",
    ],
    deps = [
        ":ivserver_lib",
        "//host/vadb:vadb_lib",
        "@gflags_repo//:gflags",
        "@glog_repo//:glog",
    ],
)

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
