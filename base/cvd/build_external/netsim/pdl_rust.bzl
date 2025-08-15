"""Run pdlc --output-format rust"""

def _pdl_rust_impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.out)
    ctx.actions.run_shell(
        tools = [ctx.executable._pdlc],
        inputs = [ctx.file.src],
        outputs = [out],
        command = "{pdlc} --output-format rust {src} > {out}".format(
            pdlc = ctx.executable._pdlc.path,
            src = ctx.file.src.path,
            out = out.path,
        ),
    )
    return DefaultInfo(files = depset([out]))

pdl_rust = rule(
    implementation = _pdl_rust_impl,
    attrs = {
        "src": attr.label(allow_single_file = True),
        "out": attr.string(mandatory = True),
        "_pdlc": attr.label(
            # Any crates with pdl-compiler would work.
            default = "@netsim_crates//:pdl-compiler__pdlc",
            executable = True,
            cfg = "exec",
        ),
    },
)
