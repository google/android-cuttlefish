cc_binary(
    name = "ivserver",
    srcs = [
            "clienthandshake.h",
            "clienthandshake.cc",
            "ivserver.h",
            "ivserver.cc",
            "layout.h",
            "main.cc",
            "options.h",
            "options.cc",
            "socketutils.h",
            "socketutils.cc",
            "utils.h",
            "utils.cc",
            "qemuhandshake.h",
            "qemuhandshake.cc",
            "vsocsharedmem.h",
            "vsocsharedmem.cc",
    ],
    copts = [
            "-I/usr/include/jsoncpp",
    ],
    linkopts = [
            "-lrt",
            "-ljsoncpp",
    ],
    deps = [
        "@glog_repo//:glog",
    ],
)

'''
cc_binary(
    name = "testclient",
    srcs =
          [
            "test_client.cc",
            "socketutils.h",
            "socketutils.cc",
          ],
)
'''

