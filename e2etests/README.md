# e2e tests 

## Golang tests

e2e tests are meant to be run with `bazel`, i.e `bazel test orchestration/...`. However,
the tests are still buildable with the `go` tool, i.e `go test ./...` should not present
any compilation erros, the execution should fail due missing data dependencies.

Adding a new dependency requires updating `go.mod` as well as `bazel` relevant files. 

```
# 1. Update go.mod.
go get <dependency> # i.e `go get github.com/google/go-cmp/cmp`
# 2. Update deps.bzl
bazel run //:gazelle -- update-repos -from_file=go.mod -to_macro=deps.bzl%go_dependencies
# 3. Update BUILD files.
bazel run //:gazelle
```
