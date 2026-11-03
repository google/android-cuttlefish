# Workspace Guidelines

This is a bazel workspace.

## Git Workflow
- Please add an "Assisted-by:" tag to git commits you create with the format: `Assisted-by: AGENTNAME:MODELVERSION`
- After modifying BUILD.bazel files, please format them with `buildozer '//cuttlefish/...:__pkg__' format`
- Every commit should be validated. Please validate changes by running `bazel build //...` and `bazel test //...`.
- To fix clang-format tests, run the `clang-format` executable directly.

## Style Rules
- Use type deduction (`auto`) only if it makes the code clearer to readers who aren't familiar with the project, or if it makes the code safer. Do not use it merely to avoid the inconvenience of writing an explicit type.
- Declare variables `const` if they are not modified after being declared.
- Prefer to separate tests into multiple files to avoid slow linter runs. clang-tidy takes 2.5 seconds per test and processes individual files synchronously.
- Bazel performs best when the build graph is wide and shallow. Accomplish this by defining each 'top-level entity' in a separate header file. This can be a class or an overload set for a free function.

