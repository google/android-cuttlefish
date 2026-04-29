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

## Usage

```
./tracebox --system-sockets -o cuttlefish_trace.ptrace --txt -c cuttlefish_trace.cfg
```

with `cuttlefish_trace.cfg` containing the following or similar:

```
buffers {
  size_kb: 4096
}
data_sources {
  config {
    name: "track_event"
    track_event_config {
      enabled_categories: "cuttlefish"
    }
  }
}
```

Next, run Cuttlefish:

```
cvd create
```

Finally, end the trace capture with Ctrl + C.

You can open https://ui.perfetto.dev/ in your webbrowser and use "Open trace
file" to view the trace.