cc_test(
    name = "circqueue_test",
    srcs = [
        "circqueue_test.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":vsoc_common",
        "//common/vsoc/shm",
        "@gtest_repo//:gtest_main",
    ],
)

cc_test(
    name = "lock_test",
    srcs = [
        "lock_test.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//common/vsoc/shm",
        "//host/vsoc/lib:vsoc_lib",
        "@gtest_repo//:gtest_main",
    ],
)

cc_test(
    name = "vsoc_graphics_test",
    srcs = [
        "graphics_test.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//common/vsoc/shm",
        "@gtest_repo//:gtest_main",
    ],
)

cc_library(
    name = "vsoc_common",
    srcs = [
        "compat.cpp",
        "e2e_test_region.cpp",
        "lock_common.cpp",
        "region_common.cpp",
    ],
    hdrs = [
        "circqueue_impl.h",
        "compat.h",
        "graphics_common.h",
        "region.h",
        "single_sided_signal.h",
    ],
    copts = ["-Wno-unused-private-field"],
    visibility = ["//visibility:public"],
    deps = [
        "//common/libs/fs",
        "//common/vsoc/shm",
        "@cuttlefish_kernel//:uapi",
    ],
)
