cc_library(
    name = "threads",
    srcs = [
    ],
    hdrs = [
        "pthread.h",
        "thunkers.h",
    ],
    deps = [
        "//common/libs/time",
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
        "//common/libs/time",
        "@glog_repo//:glog",
    ],
)

