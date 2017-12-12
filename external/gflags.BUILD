cc_library(
    name = "gflags",
    srcs = [
        ":generate_config_h",
        ":generate_gflags_h",
        ":generate_gflags_declare_h",
        ":generate_gflags_completions_h",
        "src/gflags.cc",
        "src/gflags_completions.cc",
        "src/gflags_reporting.cc",
        "src/mutex.h",
        "src/util.h",
    ],
    hdrs = [
        "gflags/gflags.h",
        "gflags/gflags_completions.h",
        "gflags/gflags_declare.h",
    ],
    copts = [
        "-I$(GENDIR)/external/gflags_repo/gflags",
        "-Wno-sign-compare",
        "-Wno-unused-local-typedefs",
    ],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "generate_config_h",
    srcs = ["src/config.h.in"],
    outs = ["gflags/config.h"],
    cmd = ("sed -e 's/#cmakedefine OS_WINDOWS//'" +
           "    -e 's/#cmakedefine HAVE_SHLWAPI_H//'" +
           "    -e 's/#cmakedefine HAVE_STRTOQ//'" +
           "    -e 's/#cmakedefine/#define/'" +
           "    -e 's/@PROJECT_NAME@/gflags/g'" +
           "    -e 's/@PACKAGE_NAME@/gflags/g'" +
           "    -e 's/@PACKAGE_STRING@/gflags 2.1.2/g'" +
           "    -e 's/@PACKAGE_TARNAME@/gflags-2.1.2/g'" +
           "    -e 's/@PACKAGE_VERSION@/2.1.2/g'" +
           "    -e 's/@PACKAGE_BUGREPORT@//g'" +
           "  $< > $@"),
)

genrule(
    name = "generate_gflags_h",
    srcs = ["src/gflags.h.in"],
    outs = ["gflags/gflags.h"],
    cmd = ("sed -e 's/@GFLAGS_ATTRIBUTE_UNUSED@/__attribute((unused))/g'" +
           "    -e 's/@INCLUDE_GFLAGS_NS_H@//g'" +
           "  $< > $@"),
)

genrule(
    name = "generate_gflags_completions_h",
    srcs = ["src/gflags_completions.h.in"],
    outs = ["gflags/gflags_completions.h"],
    cmd = ("sed -e 's/@GFLAGS_NAMESPACE@/google/g'" +
           "  $< > $@"),
)

genrule(
    name = "generate_gflags_declare_h",
    srcs = ["src/gflags_declare.h.in"],
    outs = ["gflags/gflags_declare.h"],
    cmd = ("sed -e 's/@GFLAGS_NAMESPACE@/google/g'" +
           "    -e 's/@GFLAGS_IS_A_DLL@/0/g'" +
           "    -e 's/@HAVE_STDINT_H@/1/g'" +
           "    -e 's/@HAVE_SYS_TYPES_H@/1/g'" +
           "    -e 's/@HAVE_INTTYPES_H@/1/g'" +
           "    -e 's/@GFLAGS_INTTYPES_FORMAT_C99@/1/g'" +
           "    -e 's/@GFLAGS_INTTYPES_FORMAT_BSD@/0/g'" +
           "    -e 's/@GFLAGS_INTTYPES_FORMAT_VC7@/0/g'" +
     "  $< > $@"),
)

