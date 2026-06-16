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
