# Autodetecting GPUs for Use in Docker

When running Cuttlefish directly on a host machine, you can now take advantage
of physical GPUs by some vendors. For example, to use NVIDIA GPUs, you can
launch Cuttlefish by passing the -gpu_mode=drm_virgl command-line option.

We want this ability when running Cuttlefish inside a docker container.  This is
made possible by the fact that docker containers for Cuttlefish are meant to be
built separately for each host that's running them (in other words, they are not
intended to be pulled from a central repository and just run.)  This is
primarily because we want to be able to map volumes containing prebuilt Android
images directly into the container, rather than having to copy them from the
host to the container, which adds time and wastes storage.  This requirement in
turn implies that we must pass the user's UID to when we build the docker image.

Having to build the image separately on each host makes it possible for us to
re-create the environment that we need to access the GPU.  It is important, in
re-creating this environment, to do so exactly.  Since the graphics stack in the
host has dependencies on kernel modules that are already loaded into the host's
kernel, and since the docker container will be using the same kernel, it follows
that the userspace portion of the graphics stuck must be identical to that of
the host.

The simple thing to do would have been to access the debian repositories the
host uses and to download the same packages at the same versions that the host
is using.  This discovery process is difficult for non-standard flavors of
Debian; it is also not possible to discover the necessary repositories
programmatically.  Furthermore, it is imperative to install only the non-kernel
parts of the graphics stack inside the docker container--we cannot mess with
kernel modules from within a container.

For the reasons above, we instead re-create the subset of the dependency
graph for the debian modules that comprise the user-space part of the graphics
stack on the host itself.  We discover the relevant packages on the host,
extract their debian files, and install them in the correct order on the
container image, as follows:

1. When the build script is invoked, it will scan the gpu/ directory for any
   folders.  For each of these folders, it will look for an execute a script
   called probe.sh.
2. The probe.sh script is responsible for detecting its GPU.  The initial
   implementation for NVIDIA just greps the lspci output for 'nvidia'.  The
   script returns 0 for success and nonzero for failure.
3. The first probe.sh that succeeds wins.  A host with more than one GPU is
   rare, and GPUs from different vendors should be rarer still.  The environment
   variable OEM is set within build.sh to the name of the vendor. We'll use
   'nvidia' for the rest of the document.
4. The build.sh script then invokes gpu/nvidia/driver.sh to discover the debian
   packages on the host that we need to re-create on the image.  The initial
   implementation of gpu/nvidia/driver.sh invokes gpkg-query and greps for
   packages with 'nvidia' in their names.
5. Each package from the list from the step above is then fed to a script called
   walk-deps.sh at the top level of this project.  Script walk-deps.sh walks the
   dependency graph of this package, recursively, while recording the name and
   version of each dependency in a file the first time the dependency is
   encountered.
6. Once a top-level package is processed as in the step above, we then attempt
   to extract the debian package for each of its dependencies into
   gpu/nvidia/driver-deps/.  We attempt this in one of two ways: first, apt-get
   download attempts to download the package at the specific version needed,
   using the host's configuration.  If this fails, we invoke dpkg-repack to
   attempt to recover the package offline. If this too fails, we fail the build
   process.
7. In steps 5 and 6, we use a filter script to exclude the packages we do not
   want to install onto the image.  These include the kernel components, and
   also others that we know aren't necessary.  The filter script is at
   gpu/nvidia/filter-out-deps.sh.  A complementary script,
   gpu/nvidia/filter-in-deps.sh also exists--it calls filter-out-deps.sh and
   returns the opposite result.
8. Once we have downloaded all the dependencies for the top-level package, we
   identify (by filtering on those same dependencies using filter-in-deps.sh),
   the set of packages that we need to fake out.  We want to create dummy
   packages for them so that we can satisfy the dependencies of the packages we
   create in step 7 when we later install them onto the image.  We place the
   list of dummt packages we want to create in
   gpu/nvidia/driver-deps/equivs.txt.
9. We repeat steps 5 through 9 for each package enumerated in step 4. All lists
   computed in steps 7 and 8 are merged into two files, respectively
   gpu/nvidia/driver-deps/deps.txt and gpu/nvidia/driver-deps/equivs.txt

Once we have prepared everything on the host, we invoke the docker build:

10. The docker build is split into two stages.  The first stage builds a docker
    image that is sufficient to run Cuttlefish with a software GPU.  We always
    build that stage.
11. If we have detected a physical GPU, and the user has not overridden the
    decision by passing the flag --nodetect_gpu to build.sh, we also build the
    second stage.  The second stage is pointed at all of the information we
    collected above in steps 1 through 9.  The second stage does two things.
    First, using gpu/nvidia/driver-deps/equivs.txt, it invokes the equivs
    utility to construct the dummy packages.  Second, it uses
    gpu/nvidia/driver-deps/deps.txt to installs the real packages we generated
    in step 6.  At the end of the second-stage build, we invoke dpkg -C to do a
    sanity check and confirm that the image is in a sane state.
