# Developing `cvd`

`cvd` is a command line tool that is the interface to running Cuttlefish host
executables from AOSP. It is deployed through the `cuttlefish-base` debian
package, but can also be compiled and run on its own.

## Compiling and running

```sh
bazel run cuttlefish:cvd -- reset -y
```

## Running the unit tests

```sh
bazel test '...'
```

## Autocompletion with `compile_commands.json`

```sh
bazel run @hedron_compile_commands//:refresh_all
```
creates a `compile_commands.json` file which text editors / LSP servers can use
to implement autocompletion and cross references. This is a one-shot command and
the file will become stale if new source or header files are added.
