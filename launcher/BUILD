load("//tools:py_bundle.bzl", "py_bundle")

py_library(
    name = "ivserver_lib",
    srcs = [
        "file_partition.py",
        "guest_definition.py",
        "ivserver.py",
        "libvirt_client.py",
    ],
    srcs_version = "PY2AND3",
)

py_bundle(
    name = "launcher",
    srcs = [":ivserver_lib"],
    main = "host.launcher.ivserver",
)
