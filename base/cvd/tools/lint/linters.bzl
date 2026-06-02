load("@aspect_rules_lint//lint:clang_tidy.bzl", "lint_clang_tidy_aspect")
load("@aspect_rules_lint//lint:lint_test.bzl", "lint_test")
load("@aspect_rules_lint//lint:shellcheck.bzl", "lint_shellcheck_aspect")

clang_tidy = lint_clang_tidy_aspect(
    binary = "@@//tools/lint:clang_tidy",
    configs = ["@@//:clang_tidy_config"],
    rule_kinds = ["cc_binary", "cc_library", "cc_test"],
)

clang_tidy_test = lint_test(aspect = clang_tidy)

shellcheck = lint_shellcheck_aspect(
    binary = "@aspect_rules_lint//lint:shellcheck_bin",
    config = "@@//:.shellcheckrc",
)

shellcheck_test = lint_test(aspect = shellcheck)
