load("@aspect_rules_lint//lint:buildifier.bzl", "lint_buildifier_aspect")
load("@aspect_rules_lint//lint:clang_tidy.bzl", "lint_clang_tidy_aspect")
load("@aspect_rules_lint//lint:lint_test.bzl", "lint_test")
load("@aspect_rules_lint//lint:shellcheck.bzl", "lint_shellcheck_aspect")
load("@depend_on_what_you_use//dwyu/cc:defs.bzl", "dwyu_cc_aspect_factory")

buildifier = lint_buildifier_aspect(
    binary = Label("@buildifier_prebuilt//:buildifier"),
)

buildifier_test = lint_test(aspect = buildifier)

clang_tidy = lint_clang_tidy_aspect(
    binary = "@@//tools/lint:clang_tidy",
    configs = ["@@//:clang_tidy_config"],
    rule_kinds = ["cc_binary", "cc_library", "cc_test"],
)

clang_tidy_test = lint_test(aspect = clang_tidy)

depend_on_what_you_use = dwyu_cc_aspect_factory(
    preprocessing_mode = "fast",
    target_mapping = Label("@//tools/lint:map_external_deps")
)

def _dwyu_rule_impl(ctx):
    # gather artifacts to make sure the DWYU aspect is executed
    dwyu_artifacts = depset(transitive = [dep[OutputGroupInfo].dwyu for dep in ctx.attr.deps])
    return [DefaultInfo(files = dwyu_artifacts)]

dwyu_rule = rule(
    implementation = _dwyu_rule_impl,
    attrs = {
        # You can control what this rule does (e.g. recursive vs. non recursive analysis) by specifying a DWYU aspect
        # which is configured in the desired way here.
        "deps": attr.label_list(aspects = [depend_on_what_you_use]),
        # Some DWYU flags can be controlled via '--aspects_parameters'. If their default value is 'False', they have to
        # be set explicitly by the rule wrapper.
        "dwyu_analysis_optimizes_impl_deps": attr.bool(default = False),
        "dwyu_verbose": attr.bool(default = False),
    },
)

shellcheck = lint_shellcheck_aspect(
    binary = "@aspect_rules_lint//lint:shellcheck_bin",
    config = "@@//:.shellcheckrc",
)

shellcheck_test = lint_test(aspect = shellcheck)
