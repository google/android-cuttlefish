#!/usr/bin/env python3
import ast
import glob
import os
import shutil
import stat
import sys

EMPTY_REPOS = [
    "apple_support",
    "buildifier_prebuilt",
    "buildozer",
    "dav1d",
    "depend_on_what_you_use",
    "gazelle",
    "glslang",
    "hedron_compile_commands",
    "hermetic_cc_toolchain",
    "nv_codec_headers",
    "rules_cuda",
    "rules_foreign_cc",
    "rules_go",
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
    "android_system_core": ["__subpackages__", "all", "android_system_core", "cutils_android_filesystem_config", "cutils_list", "init", "lib", "libbase", "libcrypto_utils", "libcutils", "libext4_utils", "liblog", "liblp", "libsparse", "libutils", "libziparchive", "simg2img"],
    "android_system_extras": ["__subpackages__", "all", "android_system_extras", "lib", "libext4_utils", "lpadd", "lpmake"],
    "android_system_libbase": ["__subpackages__", "all", "android_system_libbase", "cmsg", "collections", "endian", "errno_restorer", "errors", "expected", "file_logging", "format", "lib", "macros", "mapped_file", "no_destructor", "off64_t", "parsebool", "parseint", "parsenetaddress", "posix_strerror_r", "properties", "result", "scopeguard", "stringprintf", "strings", "thread_annotations", "threads", "unique_fd"],
    "android_system_logging": ["__subpackages__", "all", "android_system_logging", "lib", "liblog"],
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
    "venus_protocol": ["all", "lib", "venus_protocol", "vn_protocol_driver", "vn_protocol_renderer", "vulkan_headers"],
    "virglrenderer": ["all", "lib", "libvirglrenderer.so.1", "mesa_util", "virgl_render_server", "virglrenderer", "virglrenderer_internal"],
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
    "aspect_rules_lint/lint/buildifier.bzl": "def lint_buildifier_aspect(**kwargs):\n    return None\n",
    "aspect_rules_lint/lint/clang_tidy.bzl": "def lint_clang_tidy_aspect(**kwargs):\n    return None\n",
    "aspect_rules_lint/lint/lint_test.bzl": "def _noop(**kwargs):\n    pass\ndef lint_test(**kwargs):\n    return _noop\n",
    "aspect_rules_lint/lint/shellcheck.bzl": "def lint_shellcheck_aspect(**kwargs):\n    return None\n",
    "avb/BUILD": """\
cc_library(name = "libavb", deps = ["@boringssl//:crypto"], visibility = ["//visibility:public"])
exports_files(["avbtool.py"])
alias(name = "avb", actual = ":libavb", visibility = ["//visibility:public"])
alias(name = "lib", actual = ":libavb", visibility = ["//visibility:public"])
alias(name = "all", actual = ":libavb", visibility = ["//visibility:public"])
""",
    "avb/avbtool.py": "#!/usr/bin/env python3\n",
    "avb/MODULE.bazel": 'module(name = "avb")\nbazel_dep(name = "boringssl")\nbazel_dep(name = "rules_cc")\n',
    "buildifier_prebuilt/BUILD": 'package(default_visibility = ["//visibility:public"])\nfilegroup(name = "buildifier")\n',
    "buildifier_prebuilt/defs.bzl": """\
def _dummy_repo_impl(ctx):
    ctx.file("BUILD.bazel", "")
    ctx.file("WORKSPACE", "")

_dummy_repo = repository_rule(implementation = _dummy_repo_impl)

def _impl(module_ctx):
    _dummy_repo(name = "buildifier_prebuilt_toolchains")

buildifier_prebuilt_deps_extension = module_extension(implementation = _impl)
""",
    "depend_on_what_you_use/dwyu/cc/defs.bzl": """\
def _dummy_aspect_impl(target, ctx):
    return [OutputGroupInfo(dwyu = depset())]

_dummy_aspect = aspect(implementation = _dummy_aspect_impl)

def dwyu_cc_aspect_factory(**kwargs):
    return _dummy_aspect

MAP_DIRECT_DEPS = {}

def dwyu_make_cc_info_mapping(**kwargs):
    pass
""",
    "gazelle/extensions.bzl": """\
def _dummy_repo_impl(ctx):
    ctx.file("BUILD.bazel", "")
    ctx.file("WORKSPACE", "")

_dummy_repo = repository_rule(implementation = _dummy_repo_impl)

def _impl(ctx):
    _dummy_repo(name = "com_github_google_go_cmp")

go_deps = module_extension(
    implementation = _impl,
    tag_classes = {"from_file": tag_class(attrs = {"go_mod": attr.label()})},
)
""",
    "hermetic_cc_toolchain/toolchain/ext.bzl": """\
def _dummy_repo_impl(ctx):
    ctx.file("BUILD.bazel", "")
    ctx.file("WORKSPACE", "")

_dummy_repo = repository_rule(implementation = _dummy_repo_impl)

def _impl(module_ctx):
    _dummy_repo(name = "zig_sdk")

toolchains = module_extension(implementation = _impl)
""",
    "rules_cuda/cuda/extensions.bzl": """\
def _dummy_repo_impl(ctx):
    ctx.file("BUILD.bazel", \"\"\"\\
load("@rules_cc//cc:defs.bzl", "cc_library")
package(default_visibility = ["//visibility:public"])
cc_library(name = "cudart_headers")
\"\"\")
    ctx.file("WORKSPACE", "")

_dummy_repo = repository_rule(implementation = _dummy_repo_impl)

def _impl(module_ctx):
    _dummy_repo(name = "cuda")

toolchain = module_extension(
    implementation = _impl,
    tag_classes = {
        "redist_json": tag_class(attrs = {"name": attr.string(), "version": attr.string()}),
        "toolkit": tag_class(attrs = {"name": attr.string()}),
    },
)
""",
    "rules_go/go/extensions.bzl": """\
go_sdk = module_extension(
    implementation = lambda ctx: None,
    tag_classes = {"download": tag_class(attrs = {"version": attr.string()})},
)
""",
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

rust = module_extension(
    implementation = lambda ctx: None,
    tag_classes = {"toolchain": tag_class(attrs = {"edition": attr.string(), "versions": attr.string_list()})},
)

rust_host_tools = module_extension(
    implementation = _impl,
    tag_classes = {"host_tools": tag_class(attrs = {"name": attr.string(), "version": attr.string()})},
)
""",
    "rules_rust/crate_universe/extensions.bzl": """\
def _dummy_repo_impl(ctx):
    ctx.file("BUILD.bazel", \"\"\"\\
load("@rules_cc//cc:defs.bzl", "cc_library")
package(default_visibility = ["//visibility:public"])
cc_library(name = "crosvm__crosvm")
cc_library(name = "vhost-device-vsock__vhost-device-vsock")
\"\"\")
    ctx.file("WORKSPACE", "")

_dummy_repo = repository_rule(implementation = _dummy_repo_impl)

def _impl(ctx):
    for mod in ctx.modules:
        for tag in getattr(mod.tags, "from_specs", []):
            _dummy_repo(name = tag.name)
        for tag in getattr(mod.tags, "from_cargo", []):
            _dummy_repo(name = tag.name)

crate = module_extension(
    implementation = _impl,
    tag_classes = {
        "spec": tag_class(attrs = {
            "artifact": attr.string(),
            "default_features": attr.bool(),
            "features": attr.string_list(),
            "git": attr.string(),
            "package": attr.string(),
            "repositories": attr.string_list(),
            "rev": attr.string(),
            "version": attr.string(),
        }),
        "annotation": tag_class(attrs = {
            "additive_build_file_content": attr.string(),
            "build_script_data": attr.string_list(),
            "build_script_data_glob": attr.string_list(),
            "build_script_env": attr.string_dict(),
            "build_script_rundir": attr.string(),
            "crate": attr.string(),
            "crate_features": attr.string_list(),
            "deps": attr.string_list(),
            "gen_all_binaries": attr.bool(),
            "gen_build_script": attr.string(),
            "patch_args": attr.string_list(),
            "patches": attr.string_list(),
            "repositories": attr.string_list(),
            "rustc_env": attr.string_dict(),
            "rustc_flags": attr.string_list(),
            "version": attr.string(),
        }),
        "from_specs": tag_class(attrs = {
            "cargo_config": attr.string(),
            "cargo_lockfile": attr.string(),
            "host_tools": attr.string(),
            "name": attr.string(),
        }),
        "from_cargo": tag_class(attrs = {
            "cargo_config": attr.string(),
            "host_tools": attr.string(),
            "manifests": attr.string_list(),
            "name": attr.string(),
        }),
    },
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
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
    repos = set(EMPTY_REPOS)
    packages = set()

    pkg_targets = {pkg: set(targets) for pkg, targets in PKG_TARGETS.items()}
    for build_file in glob.glob(os.path.join(repo_root, "base/cvd/build_external/*/BUILD.*.bazel")):
        repo = os.path.basename(os.path.dirname(build_file))
        if f"{repo}/BUILD" in CUSTOM_FILES:
            continue
        try:
            with open(build_file, "r") as f:
                tree = ast.parse(f.read(), filename=build_file)
        except SyntaxError:
            continue
        names = pkg_targets.setdefault(repo, set())
        names.update([repo, "lib", "all", "__subpackages__"])
        for node in ast.walk(tree):
            if isinstance(node, ast.Call):
                for kw in node.keywords:
                    if kw.arg == "name" and isinstance(kw.value, ast.Constant) and isinstance(kw.value.value, str):
                        names.add(kw.value.value)

    for pkg, targets in pkg_targets.items():
        repos.add(pkg.split("/")[0])
        packages.add(pkg)
        pkg_dir = os.path.join(target_dir, pkg)
        os.makedirs(pkg_dir, exist_ok=True)
        with open(os.path.join(pkg_dir, "BUILD"), "w") as f:
            for t in sorted(targets):
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

    sys_rules_cc = "/usr/share/bazel/rules/cc"
    if os.path.exists(sys_rules_cc):
        dst_rules_cc = os.path.join(target_dir, "rules_cc")
        shutil.copytree(sys_rules_cc, dst_rules_cc, dirs_exist_ok=True)
        ext_path = os.path.join(dst_rules_cc, "cc", "extensions.bzl")
        if os.path.exists(ext_path):
            with open(ext_path, "a") as f:
                f.write("""
def _cc_compatibility_proxy_repo_impl(ctx):
    ctx.file("BUILD.bazel", 'package(default_visibility = ["//visibility:public"])\\n')
    ctx.file("WORKSPACE", "")
    ctx.file("proxy.bzl", \"\"\"\\
cc_binary = native.cc_binary
cc_library = native.cc_library
cc_test = native.cc_test
\"\"\")

_cc_compatibility_proxy_repo = repository_rule(implementation = _cc_compatibility_proxy_repo_impl)

def _compatibility_proxy_impl(ctx):
    _cc_compatibility_proxy_repo(name = "cc_compatibility_proxy")

compatibility_proxy = module_extension(implementation = _compatibility_proxy_impl)
""")

    repo_rule_repos = set()
    explicit_bzlmod = set()
    mod_files = (
        [os.path.join(repo_root, "base/cvd/MODULE.bazel")]
        + glob.glob(os.path.join(repo_root, "base/cvd/**/*.MODULE.bazel"), recursive=True)
        + glob.glob(os.path.join(target_dir, "*/MODULE.bazel"))
    )
    for mod_file in mod_files:
        if not os.path.exists(mod_file):
            continue
        with open(mod_file, "r") as f:
            src = f.read()
        try:
            tree = ast.parse(src, filename=mod_file)
        except SyntaxError:
            continue
        modified = False
        for node in ast.walk(tree):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                if node.func.id in ("git_repository", "http_archive"):
                    for kw in node.keywords:
                        if kw.arg == "name" and isinstance(kw.value, ast.Constant):
                            repo_rule_repos.add(kw.value.value)
                            repos.add(kw.value.value)
                    if node.func.id == "git_repository":
                        for kw in node.keywords:
                            if kw.arg == "patch_strip" and isinstance(kw.value, ast.Constant) and isinstance(kw.value.value, int):
                                kw.arg = "patch_args"
                                kw.value = ast.List(elts=[ast.Constant(value=f"-p{kw.value.value}")], ctx=ast.Load())
                                modified = True
                elif node.func.id == "bazel_dep":
                    for kw in node.keywords:
                        if kw.arg == "name" and isinstance(kw.value, ast.Constant):
                            explicit_bzlmod.add(kw.value.value)
                elif node.func.id in ("single_version_override", "git_override", "archive_override"):
                    for kw in node.keywords:
                        if kw.arg == "module_name" and isinstance(kw.value, ast.Constant):
                            explicit_bzlmod.add(kw.value.value)
                            repos.add(kw.value.value)
                    if node.func.id in ("git_override", "archive_override"):
                        new_kws = [kw for kw in node.keywords if kw.arg not in ("build_file", "files")]
                        if len(new_kws) != len(node.keywords):
                            node.keywords = new_kws
                            modified = True
        if modified:
            with open(mod_file, "w") as f:
                f.write(ast.unparse(tree) + "\n")

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

    bzlmod_modules = (repos - repo_rule_repos) | explicit_bzlmod
    override_args = []
    if os.path.exists(os.path.join(target_dir, "rules_cc")):
        override_args.append(f"--override_module=rules_cc={os.path.join(target_dir, 'rules_cc')}")
    for r in sorted(repos):
        repo_dir = os.path.join(target_dir, r)
        if r in bzlmod_modules:
            override_args.append(f"--override_module={r}={repo_dir}")
        override_args.append(f"--override_repository={r}={repo_dir}")
        override_args.append(f"--override_repository=_main~_repo_rules~{r}={repo_dir}")

    with open(os.path.join(target_dir, "override_args.txt"), "w") as f:
        f.write(" ".join(override_args) + "\n")


if __name__ == "__main__":
    main()
