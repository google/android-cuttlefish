# Adjusting APT Sources for Multiarch

The Cuttlefish host Debian packages can also be built and used on an `arm64`
based system. However, because certain parts of it are still `amd64`, the
APT sources of the system need to be adjusted for multiarch so that package
dependencies can be correctly looked up and installed.

For detailed context, see [Multiarch HOWTO](https://wiki.debian.org/Multiarch/HOWTO), and this document will use Ubuntu 21.04 (Hirsute) as an example for
making such adjustments.

The basic idea is to first limit the existing APT sources to `arm64` only,
so that when a new architecture like `amd64` is added, APT won't try to
fetch packages for the new architecture from the existing repository, as
`arm64` packages are in "ports", while `amd64` ones are in the main
repository. So a line in `/etc/apt/sources.list` such as:

```
deb http://ports.ubuntu.com/ubuntu-ports hirsute main restricted
```

would be changed to:

```
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports hirsute main restricted
```

Next, each line of config like the above will be duplicated and modified into
an entry that corresponds to what's in the main repository, with its
architecture limited to `amd64`. For example, for the same line as shown above,
a new entry will be added like this:

```
deb [arch=amd64] http://archive.ubuntu.com/ubuntu hirsute main restricted
```

The script below might be handy for this task:
```bash
#!/bin/bash
cp /etc/apt/sources.list ~/sources.list.bak
(
  (grep ^deb /etc/apt/sources.list | sed 's/deb /deb [arch=arm64] /') && \
  (grep ^deb /etc/apt/sources.list | sed 's/deb /deb [arch=amd64] /g; s/ports\.ubuntu/archive.ubuntu/g; s/ubuntu-ports/ubuntu/g') \
) | tee /tmp/sources.list
mv /tmp/sources.list /etc/apt/sources.list
```
**Note:** please run the above script as `root`, and adjust for differences in
Ubuntu releases or location prefixed repositories for faster download (e.g.
`us.archive.ubuntu.com` instead of `archive.ubuntu.com`).

Finally, add the new architecture and do an APT update with:
```bash
sudo dpkg --add-architecture amd64
sudo apt update
```
Make sure there's no errors or warnings in the output of `apt update`. To
restore the previous APT sources list, use the backup file `sources.list.bak`
saved by the script in your home directory.
