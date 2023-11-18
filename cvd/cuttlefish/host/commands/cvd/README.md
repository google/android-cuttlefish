Frontend command line interface for Cuttlefish command-line tools.

Separated into a thin client and a persistent background server process that
tracks user state.

Requests are handled through a handler classes, which may delegate into other
handler classes through `CommandSequenceExecutor`.

[![Handler diagram](./doc/all_handlers.png)](https://cs.android.com/android/platform/superproject/+/master:device/google/cuttlefish/host/commands/cvd/doc/all_handlers.svg)

A specific example of handlers delegating into other handlers to implement some
functionality:

[![Load config diagram](./doc/load_config.png)](https://cs.android.com/android/platform/superproject/+/master:device/google/cuttlefish/host/commands/cvd/doc/load_config.svg)
