#!/usr/bin/env python3
import ast
import glob
import os
import re
import shutil
import stat
import sys

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
    "aspect_bazel_lib/lib/expand_template.bzl": """\
def expand_template(name, template, out, substitutions = {}, **kwargs):
    native.genrule(
        name = name,
        srcs = [template],
        outs = [out],
        cmd = "cp $< $@",
    )
""",
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
    "bazel_features/features.bzl": "bazel_features = struct(globals = struct(), external_deps = struct(extension_metadata_has_reproducible = True))\n",
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
    "rules_python/python/config_settings/BUILD": """\
load(":defs.bzl", "string_flag")
package(default_visibility = ["//visibility:public"])
string_flag(name = "bootstrap_impl", build_setting_default = "system_python")
""",
    "rules_python/python/config_settings/defs.bzl": """\
def _impl(ctx):
    return []

string_flag = rule(
    implementation = _impl,
    build_setting = config.string(flag = True),
)
""",
    "rules_proto_grpc_cpp/defs.bzl": "def cpp_grpc_library(**kwargs):\n    pass\n",
    "rules_proto_grpc_go/defs.bzl": """\
def go_proto_compile(**kwargs): pass
def go_grpc_compile(**kwargs): pass
def go_proto_library(**kwargs): pass
def go_grpc_library(**kwargs): pass
""",
    "rules_rust/rust/defs.bzl": """\
def rust_binary(name, **kwargs):
    native.genrule(name = name, outs = [name + "_stub"], executable = True, cmd = "touch $@")
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
    "toolchains_llvm/toolchain/extensions/llvm.bzl": """\
def _dummy_repo_impl(ctx):
    ctx.file("BUILD", "")
    ctx.file("WORKSPACE", "")

_dummy_repo = repository_rule(implementation = _dummy_repo_impl)

def _impl(module_ctx):
    _dummy_repo(name = "llvm_toolchain")

llvm = module_extension(
    implementation = _impl,
    tag_classes = {"toolchain": tag_class(attrs = {"llvm_version": attr.string()})},
)
""",
}

LABEL_RE = re.compile(r"^@([A-Za-z0-9_.-]+)(?://([A-Za-z0-9_./-]*))?(?::([A-Za-z0-9_./+-]+))?$")

# Repos provided by system packages or handled by module extensions (not overridden as empty modules).
SYSTEM_OR_EXTENSION_REPOS = {
    "bazel_skylib",
    "bazel_tools",
    "cc_compatibility_proxy",
    "com_github_google_go_cmp",
    "cuda",
    "llvm_toolchain",
    "platforms",
    "rules_cc",
    "rules_license",
    "rules_proto",
    "rules_shell",
    "rust_host_tools_nightly",
    "zig_sdk",
}


def make_crate_universe_bzl(crate_targets):
    rules = "\n".join(
        f'genrule(name = "{t}", outs = ["{t}_bin"], executable = True, cmd = "touch $@")'
        for t in sorted(crate_targets)
    )
    return f"""\
def _dummy_repo_impl(ctx):
    ctx.file("BUILD.bazel", \"\"\"\\
package(default_visibility = ["//visibility:public"])
{rules}
\"\"\")
    ctx.file("defs.bzl", \"\"\"\\
def aliases(**kwargs):
    return {{}}

def all_crate_deps(**kwargs):
    return []

def crate_deps(*args, **kwargs):
    return []
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
    tag_classes = {{
        "spec": tag_class(attrs = {{
            "artifact": attr.string(),
            "default_features": attr.bool(),
            "features": attr.string_list(),
            "git": attr.string(),
            "package": attr.string(),
            "repositories": attr.string_list(),
            "rev": attr.string(),
            "version": attr.string(),
        }}),
        "annotation": tag_class(attrs = {{
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
        }}),
        "from_specs": tag_class(attrs = {{
            "cargo_config": attr.string(),
            "cargo_lockfile": attr.string(),
            "host_tools": attr.string(),
            "name": attr.string(),
        }}),
        "from_cargo": tag_class(attrs = {{
            "cargo_config": attr.string(),
            "cargo_lockfile": attr.string(),
            "host_tools": attr.string(),
            "manifests": attr.string_list(),
            "name": attr.string(),
        }}),
    }},
)
"""


def main():
    if len(sys.argv) < 2:
        print("Usage: generate_mock_repos.py <target_dir> [cvd_root]", file=sys.stderr)
        sys.exit(1)

    target_dir = sys.argv[1]
    if len(sys.argv) >= 3:
        cvd_root = os.path.abspath(sys.argv[2])
    else:
        cvd_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../base/cvd"))

    repos = set()
    packages = set()
    pkg_targets = {}
    crate_targets = {"crosvm__crosvm", "vhost-device-vsock__vhost-device-vsock", "pica__pica"}

    # 1. Discover targets from base/cvd/build_external/*/BUILD.*.bazel
    for build_file in glob.glob(os.path.join(cvd_root, "build_external/*/BUILD.*.bazel")):
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

    # 2. Discover external @repo//pkg:target references from all BUILD.bazel / .bzl files under cvd_root
    build_and_bzl_files = (
        glob.glob(os.path.join(cvd_root, "**/BUILD.bazel"), recursive=True)
        + glob.glob(os.path.join(cvd_root, "**/BUILD"), recursive=True)
        + glob.glob(os.path.join(cvd_root, "**/*.bzl"), recursive=True)
    )
    for fpath in build_and_bzl_files:
        try:
            with open(fpath, "r") as f:
                tree = ast.parse(f.read(), filename=fpath)
        except SyntaxError:
            continue
        for node in ast.walk(tree):
            if isinstance(node, ast.Constant) and isinstance(node.value, str):
                m = LABEL_RE.match(node.value)
                if not m:
                    continue
                repo, pkg, target = m.group(1), m.group(2), m.group(3)
                if repo in SYSTEM_OR_EXTENSION_REPOS:
                    continue
                if repo.endswith("_crates"):
                    if target:
                        crate_targets.add(target)
                    continue
                full_pkg = f"{repo}/{pkg}" if pkg else repo
                if f"{full_pkg}/BUILD" in CUSTOM_FILES:
                    continue
                if target is None:
                    target = pkg.split("/")[-1] if pkg else repo
                names = pkg_targets.setdefault(full_pkg, set())
                names.add(target)
                names.add(full_pkg.split("/")[-1])

    # 3. Generate BUILD files for discovered external packages
    mock_bzl = """\
def _mock_target_impl(ctx):
    f = ctx.actions.declare_file(ctx.attr.name + "_stub")
    ctx.actions.write(f, "")
    return [DefaultInfo(files = depset([f])), CcInfo()]

mock_target = rule(implementation = _mock_target_impl)
"""
    for pkg, targets in pkg_targets.items():
        repos.add(pkg.split("/")[0])
        packages.add(pkg)
        pkg_dir = os.path.join(target_dir, pkg)
        os.makedirs(pkg_dir, exist_ok=True)
        with open(os.path.join(pkg_dir, "mock.bzl"), "w") as f:
            f.write(mock_bzl)
        with open(os.path.join(pkg_dir, "BUILD"), "w") as f:
            f.write('load(":mock.bzl", "mock_target")\n')
            for t in sorted(targets):
                f.write(f'mock_target(name = "{t}", visibility = ["//visibility:public"])\n')

    # 4. Write custom .bzl / BUILD / MODULE.bazel stubs
    custom_files = dict(CUSTOM_FILES)
    custom_files["rules_rust/crate_universe/extensions.bzl"] = make_crate_universe_bzl(crate_targets)
    for rel_path, content in custom_files.items():
        repos.add(rel_path.split("/")[0])
        full_path = os.path.join(target_dir, rel_path)
        dir_path = os.path.dirname(full_path)
        os.makedirs(dir_path, exist_ok=True)
        with open(full_path, "w") as f:
            f.write(content)
        if full_path.endswith((".sh", ".py")):
            os.chmod(full_path, os.stat(full_path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        packages.add(os.path.relpath(dir_path, target_dir))

    # 5. Copy system rules_cc and append cc_compatibility_proxy extension
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

    # 6. Discover all modules/repos from MODULE.bazel files and normalize Bazel 7-incompatible attrs
    repo_rule_repos = set()
    explicit_bzlmod = set()
    mod_files = (
        [os.path.join(cvd_root, "MODULE.bazel")]
        + glob.glob(os.path.join(cvd_root, "**/*.MODULE.bazel"), recursive=True)
        + glob.glob(os.path.join(target_dir, "*/MODULE.bazel"))
        + (["/usr/share/bazel/embedded_tools/MODULE.bazel"] if os.path.exists("/usr/share/bazel/embedded_tools/MODULE.bazel") else [])
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
                    is_dev = any(
                        kw.arg == "dev_dependency" and isinstance(kw.value, ast.Constant) and kw.value.value is True
                        for kw in node.keywords
                    )
                    if is_dev and mod_file.startswith(target_dir):
                        continue
                    for kw in node.keywords:
                        if kw.arg == "name" and isinstance(kw.value, ast.Constant):
                            explicit_bzlmod.add(kw.value.value)
                            if kw.value.value not in SYSTEM_OR_EXTENSION_REPOS:
                                repos.add(kw.value.value)
                elif node.func.id in ("single_version_override", "git_override", "archive_override"):
                    if node.func.id in ("git_override", "archive_override"):
                        new_kws = [kw for kw in node.keywords if kw.arg not in ("build_file", "files")]
                        if len(new_kws) != len(node.keywords):
                            node.keywords = new_kws
                            modified = True
        if any(
            isinstance(n, ast.Expr) and isinstance(n.value, ast.Call) and isinstance(n.value.func, ast.Name) and n.value.func.id == "register_toolchains"
            for n in tree.body
        ):
            new_body = []
            for n in tree.body:
                if (
                    isinstance(n, ast.Expr)
                    and isinstance(n.value, ast.Call)
                    and isinstance(n.value.func, ast.Name)
                    and n.value.func.id == "register_toolchains"
                    and any(
                        isinstance(a, ast.Constant) and isinstance(a.value, str) and ("@zig_sdk" in a.value or "@llvm_toolchain" in a.value)
                        for a in n.value.args
                    )
                ):
                    modified = True
                    continue
                new_body.append(n)
            tree.body = new_body
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

    bzlmod_modules = explicit_bzlmod - SYSTEM_OR_EXTENSION_REPOS
    override_args = []
    if os.path.exists(os.path.join(target_dir, "rules_cc")):
        override_args.append(f"--override_module=rules_cc={os.path.join(target_dir, 'rules_cc')}")
    for r in sorted(repos):
        if r in SYSTEM_OR_EXTENSION_REPOS:
            continue
        repo_dir = os.path.join(target_dir, r)
        if r in bzlmod_modules:
            override_args.append(f"--override_module={r}={repo_dir}")
        override_args.append(f"--override_repository={r}={repo_dir}")
        override_args.append(f"--override_repository=_main~_repo_rules~{r}={repo_dir}")

    with open(os.path.join(target_dir, "override_args.txt"), "w") as f:
        f.write(" ".join(override_args) + "\n")


if __name__ == "__main__":
    main()
