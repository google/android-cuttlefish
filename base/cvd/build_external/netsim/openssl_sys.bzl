"""Rules that helps specifying the implementation of openssl-sys to be
the cc_library() from a Bazel module (@openssl or @boringssl.)"""

load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _openssl_sys_env_impl(ctx):
    libs = []
    headers_depsets = []
    all_include_directories_depsets = []
    for src in ctx.attr.deps:
        for linker_input in src[CcInfo].linking_context.linker_inputs.to_list():
            for library in linker_input.libraries:
                libs.append(library.pic_static_library)

        compilation_context = src[CcInfo].compilation_context
        all_include_directories_depsets.append(compilation_context.system_includes)
        headers_depsets.append(compilation_context.headers)

    # Create a directory consist of the libraries
    lib_dir = ctx.actions.declare_directory(ctx.attr.name + "/lib_dir")
    args = ctx.actions.args()
    args.add("--output", lib_dir.path);
    args.add_all(libs, uniquify = True)
    ctx.actions.run(
        executable = ctx.executable._copy_openssl_libraries,
        arguments = [args],
        inputs = libs,
        outputs = [lib_dir],
        mnemonic = "OpensslLibDir",
        progress_message = "Collecting files for OPENSSL_LIB_DIR %{label}",
    )

    all_include_directories = depset(transitive = all_include_directories_depsets).to_list()
    all_headers = depset(transitive = headers_depsets)

    # Create a directory consists of the included headers. We can't use the
    # one from source because we need execpath on it.
    include_dir = ctx.actions.declare_directory(ctx.attr.name + "/include_dir")
    args = ctx.actions.args()
    args.add("--out", include_dir.path)
    args.add("--includes")
    args.add_all(all_include_directories)
    args.add("--headers")
    args.add_all(all_headers)
    ctx.actions.run(
        executable = ctx.executable._copy_openssl_headers,
        arguments = [args],
        inputs = all_headers,
        outputs = [include_dir],
        mnemonic = "OpensslIncludeDir",
    )

    return [
        DefaultInfo(files = depset([lib_dir, include_dir])),
        OutputGroupInfo(
            lib_dir = depset([lib_dir]),
            include_dir = depset([include_dir]),
        ),
    ]

openssl_sys_env = rule(
    implementation = _openssl_sys_env_impl,
    attrs = {
        "deps": attr.label_list(
            doc = """List of libraries from the Bazel module.

                Should be //:crypto and //:ssl from the module, e.g.
                @boringssl//:crypto, etc.""",
            providers = [CcInfo],
        ),
        "_copy_openssl_headers": attr.label(
            default = ":copy_openssl_headers",
            executable = True,
            cfg = "exec",
        ),
        "_copy_openssl_libraries": attr.label(
            default = ":copy_openssl_libraries",
            executable = True,
            cfg = "exec",
        ),
    },
)
