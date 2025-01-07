# e2e tests 

## Orchestration tests

```
bazel run //:gazelle
```

```
bazel test --local_test_jobs=1 orchestration/...
```

### Adding a new Go dependency

If adding `github.com/gorilla/websocket`

```
bazel run //:gazelle -- update-repos -to_macro=go_repositories.bzl%repos "github.com/gorilla/websocket"
```

```
bazel run //:gazelle
```
