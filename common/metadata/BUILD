cc_library(
    name = "constants",
    visibility = [ "//visibility:public" ],
    srcs = [
        "gce_metadata_attributes.cpp",
        "gce_metadata_attributes.h",
        "gce_resource_location.cpp",
        "gce_resource_location.h",
    ],
    hdrs = [
        "gce_metadata_attributes.h",
        "gce_resource_location.h",
    ],
)

cc_library(
    name = "initial_metadata_reader",
    visibility = [ "//visibility:public" ],
    srcs = [
        "display_properties.cpp",
        "display_properties.h",
        "initial_metadata_reader.h",
        "initial_metadata_reader_impl.cpp",
        "initial_metadata_reader_impl.h",
        "metadata_query.cpp",
        "metadata_query.h",
    ],
    hdrs = [
        "display_properties.h",
        "initial_metadata_reader.h",
        "metadata_query.h",
    ],
    copts = [
        "-I/usr/include/jsoncpp",
    ],
    linkopts = [
        "-ljsoncpp",
    ],
    deps = [
        "@glog_repo//:glog",
        ":constants",
        "//common/auto_resources",
        "//common/fs",
    ],
)

cc_library(
    name = "partitions",
    visibility = [ "//visibility:public" ],
    srcs = [
        "get_partition_num.cpp",
        "get_partition_num.h",
    ],
    hdrs = [
        "get_partition_num.h",
    ],
    deps = [
        "//common/auto_resources",
    ],
)

cc_test(
    name = "initial_metadata_reader_test",
    srcs = [
        "initial_metadata_reader_test.cpp",
    ],
    copts = [
        "-I/usr/include/jsoncpp",
    ],
    deps = [
        ":initial_metadata_reader",
    ],
)
