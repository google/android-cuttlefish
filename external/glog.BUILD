cc_library(
    name = "glog",
    srcs = [
        ":generate_config_h",
        ":generate_glog_logging_h",
        ":generate_glog_raw_logging_h",
        ":generate_glog_stl_logging_h",
        ":generate_glog_vlog_is_on_h",
        "src/base/commandlineflags.h",
        "src/base/googleinit.h",
        "src/base/mutex.h",
        "src/demangle.cc",
        "src/demangle.h",
        "src/logging.cc",
        "src/raw_logging.cc",
        "src/signalhandler.cc",
        "src/stacktrace.h",
        "src/stacktrace_generic-inl.h",
        "src/symbolize.cc",
        "src/symbolize.h",
        "src/utilities.cc",
        "src/utilities.h",
        "src/vlog_is_on.cc",
    ],
    hdrs = [
        "src/glog/log_severity.h",
        "src/glog/logging.h",
        "src/glog/raw_logging.h",
        "src/glog/stl_logging.h",
        "src/glog/vlog_is_on.h",
    ],
    copts = [
        "-I$(GENDIR)/external/glog_repo/src",
        "-Wno-sign-compare",
    ],
    linkopts = [ "-lpthread" ],
    visibility = [ "//visibility:public" ],
    deps = [
        "//external:gflags",
    ],
)

genrule(
    name = "generate_config_h",
    srcs = ["src/config.h.in"],
    outs = ["src/config.h"],
    cmd = ("sed -e 's/#undef \\(DISABLE_RTTI\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(GOOGLE_NAMESPACE\\)$$/#define \\1 google/'" +
           "    -e 's/#undef \\(HAVE_DLFCN_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_EXECINFO_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_FCNTL\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_GLOB_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_INTTYPES_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_LIBPTHREAD\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_LIB_GFLAGS\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_MEMORY_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_NAMESPACES\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_PREAD\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_PTHREAD\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_PWD_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_PWRITE\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_RWLOCK\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SIGACTION\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SIGALTSTACK\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_STDINT_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_STDLIB_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_STRINGS_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_STRING_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SYSCALL_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SYSLOG_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SYS_STAT_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SYS_SYSCALL_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SYS_TIME_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SYS_TYPES_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SYS_UCONTEXT_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_SYS_UTSNAME_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_UCONTEXT_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_UNISTD_H\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE_USING_OPERATOR\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE___ATTRIBUTE__\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE___BUILTIN_EXPECT\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(HAVE___SYNC_VAL_COMPARE_AND_SWAP\\)$$/#define \\1 1/'" +
           "    -e 's/#undef \\(PACKAGE\\)$$/#define \\1 \"glog\"/'" +
           "    -e 's/#undef \\(PACKAGE_BUGREPORT\\)$$/#define \\1 \"\"/'" +
           "    -e 's/#undef \\(PACKAGE_NAME\\)$$/#define \\1 \"glog\"/'" +
           "    -e 's/#undef \\(PACKAGE_STRING\\)$$/#define \\1 \"glog 0.3.4\"/'" +
           "    -e 's/#undef \\(PACKAGE_TARNAME\\)$$/#define \\1 \"glog\"/'" +
           "    -e 's/#undef \\(PACKAGE_URL\\)$$/#define \\1 \"\"/'" +
           "    -e 's/#undef \\(PACKAGE_VERSION\\)$$/#define \\1 \"0.3.4\"/'" +
           "    -e 's/#undef \\(STL_NAMESPACE\\)$$/#define \\1 std/'" +
           "    -e 's/#undef \\(VERSION\\)$$/#define \\1 \"0.3.4\"/'" +
           "    -e 's/#undef \\(_END_GOOGLE_NAMESPACE_\\)$$/#define \\1 }/'" +
           "    -e 's/#undef \\(_START_GOOGLE_NAMESPACE_\\)$$/#define \\1 namespace google {/'" +
           "  $< > $@"),
)

genrule(
    name = "generate_glog_logging_h",
    srcs = ["src/glog/logging.h.in"],
    outs = ["src/glog/logging.h"],
    cmd = ("sed -e 's/@ac_cv___attribute___noinline@/__attribute__((__noinline__))/g'" +
           "    -e 's/@ac_cv___attribute___noreturn@/__attribute__((__noreturn__))/g'" +
           "    -e 's/@ac_cv_have___builtin_expect@/1/g'" +
           "    -e 's/@ac_cv_have___uint16@/0/g'" +
           "    -e 's/@ac_cv_have_inttypes_h@/1/g'" +
           "    -e 's/@ac_cv_have_libgflags@/1/g'" +
           "    -e 's/@ac_cv_have_stdint_h@/1/g'" +
           "    -e 's/@ac_cv_have_systypes_h@/1/g'" +
           "    -e 's/@ac_cv_have_u_int16_t@/0/g'" +
           "    -e 's/@ac_cv_have_uint16_t@/1/g'" +
           "    -e 's/@ac_cv_have_unistd_h@/1/g'" +
           "    -e 's/@ac_google_end_namespace@/}/g'" +
           "    -e 's/@ac_google_namespace@/google/g'" +
           "    -e 's/@ac_google_start_namespace@/namespace google {/g'" +
           "  $< > $@"),
)

genrule(
    name = "generate_glog_raw_logging_h",
    srcs = ["src/glog/raw_logging.h.in"],
    outs = ["src/glog/raw_logging.h"],
    cmd = ("sed -e 's/@ac_cv___attribute___printf_4_5@/__attribute__((__format__(__printf__,4,5)))/g'" +
           "    -e 's/@ac_google_end_namespace@/}/g'" +
           "    -e 's/@ac_google_namespace@/google/g'" +
           "    -e 's/@ac_google_start_namespace@/namespace google {/g'" +
           "  $< > $@"),
)

genrule(
    name = "generate_glog_stl_logging_h",
    srcs = ["src/glog/stl_logging.h.in"],
    outs = ["src/glog/stl_logging.h"],
    cmd = ("sed -e 's/@ac_cv_cxx_using_operator@/1/g'" +
           "    -e 's/@ac_google_end_namespace@/}/g'" +
           "    -e 's/@ac_google_namespace@/google/g'" +
           "    -e 's/@ac_google_start_namespace@/namespace google {/g'" +
           "  $< > $@"),
)

genrule(
    name = "generate_glog_vlog_is_on_h",
    srcs = ["src/glog/vlog_is_on.h.in"],
    outs = ["src/glog/vlog_is_on.h"],
    cmd = ("sed -e 's/@ac_google_namespace@/google/g'" +
           "  $< > $@"),
)
