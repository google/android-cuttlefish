workspace(name = "avd")

new_http_archive(
    name = "gtest_archive",
    url = "https://github.com/google/googletest/archive/release-1.8.0.zip",
    sha256 = "f3ed3b58511efd272eb074a3a6d6fb79d7c2e6a0e374323d1e6bcbcc1ef141bf",
    strip_prefix = "googletest-release-1.8.0",
    build_file = "//third_party:gtest.BUILD",
)

bind(
    name = "gtest",
    actual = "@gtest_archive//:gtest",
)

bind(
    name = "gtest_main",
    actual = "@gtest_archive//:gtest_gmock",
)


