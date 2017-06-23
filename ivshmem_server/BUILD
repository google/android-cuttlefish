py_binary(
    name = "launcher",
    data = ["vsoc_mem.json"],
    srcs = ["ivserver.py"],
    main = "ivserver.py",
    default_python_version = "PY3",
    srcs_version = "PY3",
    deps = [":ivserver_lib"],
)

py_library(
    name = "ivserver_lib",
    srcs = [
        "channel.py",
        "clientconnection.py",
        "errors.py",
        "ivserver.py",
        "vmconnection.py",
        "vsocsharedmem.py",
    ],
    srcs_version = "PY2AND3",
)

