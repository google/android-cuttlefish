# Build AllocD

For now, AllocD is built as a standalone executable

## Dependency

### packages
1. libfmt-dev
2. libgoogle-glog-dev
3. libjsoncpp-dev
4. libgflags-dev (installed by libgoogle-glog-dev)

## Configure
   ```bash
   $ mkdir build && cmake -B build
   ```
   `build` directory will be created.

## Build
   ```bash
   $ cmake --build build
   ```

## Local Install
   ```bash
   $ mkdir bin lib && cmake --install build
   ```
   `bin` and `lib` directories are populated.

# Directory Structure

`include` directory keeps the headers in its subdirectories.
`srcs` keeps `.c`, `.cc`, `.cpp` files in its subdirectories.

```bash
$ tree -d
.
├── include
│   ├── android-base
│   └── cuttlefish
│       ├── allocd
│       ├── fs
│       └── utils
└── srcs
    ├── cuttlefish
    │   ├── allocd
    │   └── fs
    └── testings
```
