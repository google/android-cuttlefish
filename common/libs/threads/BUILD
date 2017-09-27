cc_library(
    name = "threads",
    srcs = [
    ],
    hdrs = [
        "pthread.h",
        "thread_annotations.h",
        "thunkers.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//common/libs/time",
    ],
)

cc_test(
    name = "threads_test",
    srcs = [
        "pthread_test.cpp",
    ],
    linkopts = ["-lpthread"],
    deps = [
        ":threads",
        "//common/libs/glog:cuttlefish_logging",
        "//common/libs/time",
    ],
)
