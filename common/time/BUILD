cc_library(
    name = "time",
    srcs = [
        "monotonic_time.cpp",
        "monotonic_time.h",
    ],
    hdrs = [
        "monotonic_time.h",
    ],
    visibility = [ "//visibility:public" ],
)

cc_test(
    name = "monotonic_time_test",
    srcs = [
        "monotonic_time_test.cpp",
    ],
    deps = [
        ":time",
        "@gtest_repo//:gtest_main",
    ],
)

