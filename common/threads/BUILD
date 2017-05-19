cc_library(
    name = "threads",
    srcs = [
    ],
    hdrs = [
        "pthread.h",
        "thunkers.h",
    ],
    deps = [
        "//common/time",
    ],
    visibility = [ "//visibility:public" ],
)

cc_test(
    name = "threads_test",
    srcs = [
        "pthread_test.cpp",
    ],
    linkopts = [ "-lpthread" ],
    deps = [
        ":threads",
        "//common/time",
        "@glog_repo//:glog",
    ],
)

