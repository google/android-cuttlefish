#!/usr/bin/env python3
import os
import stat
import sys

EMPTY_REPOS = [
    "apple_support",
    "buildozer",
    "hedron_compile_commands",
    "rules_foreign_cc",
    "rules_m4",
    "rules_nodejs",
]

PKG_TARGETS = {
    "abseil-cpp": ["abseil-cpp"],
    "abseil-cpp/absl/algorithm": ["algorithm"],
    "abseil-cpp/absl/base": ["base", "log_severity", "no_destructor"],
    "abseil-cpp/absl/cleanup": ["cleanup"],
    "abseil-cpp/absl/container": ["btree", "container", "flat_hash_map"],
    "abseil-cpp/absl/debugging": ["debugging"],
    "abseil-cpp/absl/flags": ["flag", "flags", "parse"],
    "abseil-cpp/absl/functional": ["bind_front", "functional"],
    "abseil-cpp/absl/hash": ["hash"],
    "abseil-cpp/absl/log": ["check", "globals", "initialize", "log", "log_entry", "log_sink", "log_sink_registry", "vlog_is_on"],
    "abseil-cpp/absl/memory": ["memory"],
    "abseil-cpp/absl/meta": ["meta"],
    "abseil-cpp/absl/numeric": ["numeric"],
    "abseil-cpp/absl/profiling": ["profiling"],
    "abseil-cpp/absl/random": ["bit_gen_ref", "distributions", "random"],
    "abseil-cpp/absl/status": ["status", "statusor"],
    "abseil-cpp/absl/strings": ["cord", "str_format", "strings"],
    "abseil-cpp/absl/synchronization": ["synchronization"],
    "abseil-cpp/absl/time": ["time"],
    "abseil-cpp/absl/types": ["span", "types"],
    "abseil-cpp/absl/utility": ["utility"],
    "android_system_core": ["__subpackages__", "all", "android_system_core", "init", "lib", "libbase", "libcrypto_utils", "libcutils", "libext4_utils", "liblog", "liblp", "libsparse", "libutils", "libziparchive", "simg2img"],
    "android_system_extras": ["__subpackages__", "all", "android_system_extras", "lib", "libext4_utils", "lpadd", "lpmake"],
    "android_tools_netsim": ["all", "android_tools_netsim", "lib", "netsim", "netsim-cli", "netsim-daemon", "netsimd"],
    "arm_optimized_routines": ["all", "arm_optimized_routines", "lib", "string"],
    "boringssl": ["boringssl", "crypto"],
    "brotli": ["brotlidec", "brotlienc", "brotli"],
    "bzip2": ["bzip2"],
    "c-ares": ["ares", "c-ares"],
    "casimir": ["Cargo.toml", "all", "casimir", "lib"],
    "crc32c": ["crc32c"],
    "crosvm": ["Cargo.lock", "__subpackages__", "all", "compile_seccomp_policy_zip", "crosvm", "crosvm_gpu_display_wayland_protocols", "lib", "libcrosvm", "minijail_sources"],
    "curl": ["curl"],
    "cxx.rs": ["codegen", "cxx"],
    "dosfstools": ["__subpackages__", "all", "dosfstools", "lib", "mkfs.fat"],
    "e2fsprogs": ["__subpackages__", "all", "e2fsck", "e2fsdroid", "e2fsprogs", "lib", "libext2_com_err_headers", "libext2_uuid", "mke2fs", "resize2fs"],
    "egl_headers": ["all", "egl_headers", "lib"],
    "expat": ["__subpackages__", "all", "expat", "lib"],
    "f2fs_tools": ["__subpackages__", "all", "f2fs_tools", "fsck.f2fs", "lib", "make_f2fs"],
    "fec_rs": ["all", "fec_rs", "lib", "libfec_rs"],
    "fmt": ["fmt"],
    "freetype": ["freetype"],
    "fruit": ["fruit", "lib", "all"],
    "gflags": ["gflags"],
    "gfxstream": ["__subpackages__", "all", "gfxstream", "lib"],
    "gfxstream/host": ["gfxstream_backend"],
    "googleapis": ["googleapis"],
    "googleapis-cc": ["googleapis-cc"],
    "googletest": ["gtest", "gtest_main", "gmock", "gmock_main"],
    "grpc": ["grpc++", "grpc", "grpc++_reflection"],
    "grpc-proto": ["grpc-proto"],
    "icu": ["icu"],
    "jsoncpp": ["jsoncpp"],
    "libarchive": ["libarchive"],
    "libarchive/cpio": ["cpio"],
    "libcbor": ["__subpackages__", "all", "cbor", "lib", "libcbor"],
    "libconfig": ["all", "lib", "libconfig"],
    "libdrm": ["all", "lib", "libdrm", "libdrm_fourcc"],
    "libeigen": ["all", "lib", "libeigen"],
    "libevent": ["event", "libevent"],
    "libffi": ["all", "lib", "libffi"],
    "libjpeg_turbo": ["jpeg", "libjpeg_turbo"],
    "libnl": ["__subpackages__", "all", "lib", "libnl"],
    "libopenscreen": ["__subpackages__", "all", "lib", "libopenscreen"],
    "libpffft": ["all", "lib", "libpffft"],
    "libpng": ["libpng"],
    "libsrtp2": ["all", "lib", "libsrtp2"],
    "libusb": ["all", "lib", "libusb"],
    "libuuid": ["libuuid"],
    "libvpx": ["__subpackages__", "all", "lib", "libvpx"],
    "libwebm": ["all", "lib", "libwebm", "mkvmuxer"],
    "libwebrtc": ["all", "lib", "libwebrtc"],
    "libwebsockets": ["__subpackages__", "all", "lib", "libwebsockets"],
    "libxml2": ["libxml2"],
    "libyuv": ["all", "lib", "libyuv"],
    "libzip": ["libzip"],
    "lz4": ["lz4", "lz4_frame"],
    "lz4/programs": ["lz4"],
    "mako": ["__subpackages__", "all", "lib", "mako"],
    "markupsafe": ["all", "lib", "markupsafe"],
    "mesa": ["__subpackages__", "all", "lib", "mesa", "vk_lavapipe"],
    "mkbootimg": ["all", "bootimg_header", "lib", "mkbootimg", "mkbootimg.py", "unpack_bootimg", "unpack_bootimg.py"],
    "ms-tpm-20-ref": ["all", "lib", "ms-tpm-20-ref", "simulator"],
    "mtools": ["__subpackages__", "all", "lib", "mtools"],
    "nasm": ["nasm"],
    "opengl_headers": ["GLES2_headers", "GLES3_headers", "GLES_headers", "__subpackages__", "all", "lib", "opengl_headers"],
    "pcre2": ["pcre2"],
    "protobuf/src/google/protobuf/io": ["io", "tokenizer"],
    "pyyaml": ["all", "lib", "pyyaml", "yaml"],
    "re2": ["re2"],
    "rootcanal": ["rootcanal", "rootcanal_proto", "librootcanal"],
    "rootcanal/packets": ["link_layer_packets_rs"],
    "sandboxed_api": ["sandboxed_api"],
    "selinux": ["__subpackages__", "all", "lib", "libselinux", "sefcontext_compile", "selinux"],
    "spirv_headers": ["all", "lib", "spirv_cpp_headers", "spirv_headers"],
    "spirv_tools": ["all", "lib", "spirv_tools", "spirv_tools_opt"],
    "swiftshader": ["all", "lib", "swiftshader", "swiftshader_llvm", "vk_swiftshader"],
    "tinyxml2": ["tinyxml2"],
    "tl-expected": ["tl-expected"],
    "vulkan_headers": ["all", "lib", "vulkan_headers", "vulkan_hpp"],
    "wayland": ["__subpackages__", "all", "lib", "wayland", "wayland_scanner", "wayland_server"],
    "wmediumd": ["__subpackages__", "all", "lib", "libwmediumd", "wmediumd", "wmediumd_gen_config"],
    "xz": ["xz"],
    "zlib": ["zlib"],
}

PROTOBUF_BUILD = """\
load("@rules_proto//proto:defs.bzl", "proto_lang_toolchain", "proto_toolchain", "proto_library")
package(default_visibility = ["//visibility:public"])

cc_library(name = "protobuf", linkopts = ["-lprotobuf"])
cc_library(name = "protobuf_lite", linkopts = ["-lprotobuf-lite"])
cc_library(name = "differencer", deps = [":protobuf"])
cc_library(name = "json_util", deps = [":protobuf"])
sh_binary(name = "protoc", srcs = ["protoc.sh"])
proto_lang_toolchain(name = "cc_toolchain", command_line = "--cpp_out=$(OUT)", runtime = ":protobuf")
proto_toolchain(name = "proto_toolchain", proto_compiler = ":protoc")
""" + "".join(
    f'proto_library(name = "{p}_proto", srcs = [])\n'
    f'cc_library(name = "{p}_cc_proto", deps = [":protobuf"])\n'
    for p in [
        "any", "api", "cpp_features", "descriptor", "duration", "empty",
        "field_mask", "source_context", "struct", "timestamp", "type", "wrappers",
    ]
)

CUSTOM_FILES = {
    "aspect_bazel_lib/lib/expand_template.bzl": "def expand_template(**kwargs):\n    pass\n",
    "aspect_rules_lint/format/defs.bzl": "def format_test(**kwargs):\n    pass\n",
    "avb/BUILD": """\
cc_library(name = "libavb", deps = ["@boringssl//:crypto"], visibility = ["//visibility:public"])
exports_files(["avbtool.py"])
alias(name = "avb", actual = ":libavb", visibility = ["//visibility:public"])
alias(name = "lib", actual = ":libavb", visibility = ["//visibility:public"])
alias(name = "all", actual = ":libavb", visibility = ["//visibility:public"])
""",
    "avb/avbtool.py": "#!/usr/bin/env python3\n",
    "curl/MODULE.bazel": 'module(name = "curl")\nbazel_dep(name = "c-ares")\nbazel_dep(name = "re2")\n',
    "googleapis/MODULE.bazel": 'module(name = "googleapis")\nbazel_dep(name = "protobuf")\n',
    "googletest/MODULE.bazel": 'module(name = "googletest")\nbazel_dep(name = "rules_cc")\n',
    "libarchive/MODULE.bazel": 'module(name = "libarchive")\nbazel_dep(name = "bzip2")\nbazel_dep(name = "xz")\nbazel_dep(name = "rules_m4")\n',
    "googleapis/google/rpc/BUILD": """\
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")

proto_library(name = "status_proto", srcs = ["status.proto"], visibility = ["//visibility:public"], deps = ["@protobuf//:any_proto"])
cc_proto_library(name = "status_cc_proto", deps = [":status_proto"], visibility = ["//visibility:public"])
proto_library(name = "code_proto", srcs = ["code.proto"], visibility = ["//visibility:public"])
cc_proto_library(name = "code_cc_proto", deps = [":code_proto"], visibility = ["//visibility:public"])
""",
    "grpc/bazel/cc_grpc_library.bzl": """\
def _generate_grpc_impl(ctx):
    out = []
    for t in ctx.attr.srcs:
        for src in (t[ProtoInfo].direct_sources if ProtoInfo in t else t.files.to_list()):
            b = src.basename[:-6] if src.basename.endswith(".proto") else src.basename
            cc, h = ctx.actions.declare_file(b + ".grpc.pb.cc"), ctx.actions.declare_file(b + ".grpc.pb.h")
            out.extend([cc, h])
            ctx.actions.run(
                inputs = [src], outputs = [cc, h], executable = "/usr/bin/protoc",
                arguments = ["--plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin", "--grpc_out=" + ctx.bin_dir.path, "-I.", src.path],
            )
    return [DefaultInfo(files = depset(out))]

_generate_grpc = rule(implementation = _generate_grpc_impl, attrs = {"srcs": attr.label_list()})

def cc_grpc_library(name, srcs = [], deps = [], **kwargs):
    gen = "_" + name + "_grpc_gen"
    _generate_grpc(name = gen, srcs = srcs)
    native.cc_library(
        name = name, srcs = [":" + gen], hdrs = [":" + gen],
        deps = [d for d in deps if d != "@grpc//:grpc++"] + ["@grpc//:grpc++"],
        visibility = kwargs.get("visibility", ["//visibility:public"]),
    )
""",
    "protobuf/BUILD": PROTOBUF_BUILD,
    "protobuf/MODULE.bazel": 'module(name = "protobuf")\nbazel_dep(name = "rules_proto", version = "0.0.0")\n',
    "protobuf/bazel/cc_proto_library.bzl": "cc_proto_library = native.cc_proto_library\n",
    "protobuf/bazel/proto_library.bzl": "proto_library = native.proto_library\n",
    "protobuf/protobuf_deps.bzl": "def protobuf_deps():\n    pass\n",
    "protobuf/protoc.sh": "#!/bin/sh\n",
    "rules_flex/flex/flex.bzl": "def flex_cc_library(**kwargs):\n    pass\n",
    "rules_proto_grpc_cpp/defs.bzl": "def cpp_grpc_library(**kwargs):\n    pass\n",
    "rules_proto_grpc_go/defs.bzl": """\
def go_proto_compile(**kwargs): pass
def go_grpc_compile(**kwargs): pass
def go_proto_library(**kwargs): pass
def go_grpc_library(**kwargs): pass
""",
    "rules_rust/rust/defs.bzl": """\
def rust_binary(**kwargs): pass
def rust_library(**kwargs): pass
def rust_static_library(**kwargs): pass
def rust_test(**kwargs): pass
""",
    "rules_rust/rust/extensions.bzl": """\
def _dummy_repo_impl(ctx):
    ctx.file("BUILD", "")
    ctx.file("WORKSPACE", "")

_dummy_repo = repository_rule(implementation = _dummy_repo_impl)

def _impl(ctx):
    _dummy_repo(name = "rust_host_tools_nightly")

rust_host_tools = module_extension(
    implementation = _impl,
    tag_classes = {"host_tools": tag_class(attrs = {"name": attr.string(), "version": attr.string()})},
)
""",
    "toolchains_llvm/toolchain/extensions/llvm.bzl": """\
def _dummy_repo_impl(ctx):
    ctx.file("BUILD", "")
    ctx.file("WORKSPACE", "")

_dummy_repo = repository_rule(implementation = _dummy_repo_impl)

def _impl(ctx):
    _dummy_repo(name = "llvm_toolchain")

llvm = module_extension(
    implementation = _impl,
    tag_classes = {"toolchain": tag_class(attrs = {"llvm_version": attr.string()})},
)
""",
}

def main():
    target_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "mock_repos")
    repos = set(EMPTY_REPOS)
    packages = set()

    for pkg, targets in PKG_TARGETS.items():
        repos.add(pkg.split("/")[0])
        packages.add(pkg)
        pkg_dir = os.path.join(target_dir, pkg)
        os.makedirs(pkg_dir, exist_ok=True)
        with open(os.path.join(pkg_dir, "BUILD"), "w") as f:
            for t in targets:
                f.write(f'cc_library(name = "{t}", visibility = ["//visibility:public"])\n')

    for rel_path, content in CUSTOM_FILES.items():
        repos.add(rel_path.split("/")[0])
        full_path = os.path.join(target_dir, rel_path)
        dir_path = os.path.dirname(full_path)
        os.makedirs(dir_path, exist_ok=True)
        with open(full_path, "w") as f:
            f.write(content)
        if full_path.endswith((".sh", ".py")):
            os.chmod(full_path, os.stat(full_path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        packages.add(os.path.relpath(dir_path, target_dir))

    for r in repos:
        os.makedirs(os.path.join(target_dir, r), exist_ok=True)
        packages.add(r)
        mod_path = os.path.join(target_dir, r, "MODULE.bazel")
        if not os.path.exists(mod_path):
            with open(mod_path, "w") as f:
                f.write(f'module(name = "{r}")\n')

    for pkg in packages:
        build_path = os.path.join(target_dir, pkg, "BUILD")
        if not os.path.exists(build_path):
            open(build_path, "w").close()


if __name__ == "__main__":
    main()
