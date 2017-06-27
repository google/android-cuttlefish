load("//tools:py_bundle.bzl", "py_bundle")

py_library(
    name = "ivserver_lib",
    srcs = [
        "channel.py",
        "clientconnection.py",
        "guest_definition.py",
        "ivserver.py",
        "libvirt_client.py",
        "vmconnection.py",
        "vsocsharedmem.py",
    ],
    srcs_version = "PY2AND3",
)

py_bundle(
    name = "launcher",
    srcs = [":ivserver_lib"],
    main = "host.launcher.ivserver",
)
