cc_library(
    name = "auto_resources",
    srcs = [
        "auto_resources.cpp",
        "auto_resources.h",
    ],
    hdrs = [
        "auto_resources.h",
    ],
    visibility = [ "//visibility:public" ],
)

cc_test(
    name = "auto_free_buffer_test",
    srcs = [
        "auto_free_buffer_test.cpp",
    ],
    deps = [
        ":auto_resources",
        "@gtest_repo//:gtest_main",
    ],
)

