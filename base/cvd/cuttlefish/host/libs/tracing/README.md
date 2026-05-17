# Cuttlefish Tracing

Cuttlefish host tooling supports [Perfetto](http://perfetto.dev) for tracing.

## Setup

Please see the
[Perfetto Quickstart](https://perfetto.dev/docs/getting-started/system-tracing)
instructions, or follow these potentially out of date instructions below:

```
curl -LO https://get.perfetto.dev/tracebox
chmod +x tracebox
```

and add `tracebox` to your `$PATH`.

## Usage

```
tracebox --system-sockets -o cuttlefish_trace.ptrace --txt -c cuttlefish_trace.cfg
```

with `cuttlefish_trace.cfg` containing the following or similar:

```
buffers {
  size_kb: 4096
}
data_sources: {
  config {
    name: "track_event"
    track_event_config {
      disabled_categories: "*"
      enabled_categories: "cuttlefish"
    }
  }
}
```

Next, run Cuttlefish:

```
CVD_TRACE=1 cvd create
```

TODO(b/324031921): revisit the need for `CVD_TRACE=1` after fixed.

Finally, end the trace capture with Ctrl + C.

You can open https://ui.perfetto.dev/ in your webbrowser and use "Open trace
file" to view the trace.