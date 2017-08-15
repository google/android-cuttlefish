cc_binary(
    name = "cf",
    srcs = [
        "main.cc",
    ],
    copts = [
        "-I/usr/include/jsoncpp",
        "-I/usr/include/libxml2",
    ],
    linkopts = [
        # TODO(ender): find a better way to manage dependencies across
        # incompatible systems, like our obsolete Goobuntu vs modern Debian distros.
        # The following libraries must be linked statically
        # to avoid problems with missing (obsolete) library versions on newer systems.
        "-l:libjsoncpp.a",
        "-ludev",
        "-lxml2",
        "-lvirt",
    ],
    deps = [
        "//host/config:config_lib",
        "//host/ivserver:ivserver_lib",
        "//host/vadb:vadb_lib",
        "@gflags_repo//:gflags",
        "@glog_repo//:glog",
    ],
)


