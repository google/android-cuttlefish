RAMDISK_GEN_SUFFIX="__files__"

#
# |ramdisk_files| is just a container holding names of files that will be included
# in target ramdisk. This container can hold any artifacts or file sets.
#
# |ramdisk_files| are used directly by |ramdisk_create| to collect list of files
# that should be included in target ramdisk.
#
# TL;DR:
#
#    ramdisk_files(srcs[A, B, C], out=D) => struct(files=[A, B, C], directory=D)
#
def _ramdisk_files(ctx):
  outs = set()

  # Just keep a list of files to copy
  for files in ctx.attr.srcs:
    outs += files.files

  return struct(files=outs, directory=ctx.attr.out)


#
# |ramdisk_create| collects artifacts defined by |ramdisk_files| and puts them
# in a gzip-compressed cpio archive (a.k.a. initrd file).
#
# Because we can't simply collect files from where they are (because cpio uses
# source file location as its name in the archive) this is a two step function:
# 1) Iterate through all the sources and copy all files to a directory holding
#    temporary structure of init ramdisk (= create unpacked ramdisk).
#    NOTE:
#    Do not attempt to create symlinks, otherwise you'll end up working with
#    dangling symlinks and build failures.
# 2) scan all files that we collected (using 'find') and redirect output to cpio.
#
# TL;DR:
#
#   for src in ${sources}; do cp ${src} ${out}__files__; done
#   date > ${out}__files__/.build
#   ( cd ${out}__files__; find . | cpio | gzip ) > output
#
def _ramdisk_create(ctx):
  # List of files that have to be included in target ramdisk build.
  temp_outs = list([])
  # Location of temporary ramdisk.
  temp_ramdisk_loc = ctx.attr.out + RAMDISK_GEN_SUFFIX

  #
  # Iterate all files and create symlinks.
  #
  for source in ctx.attr.srcs:
    for source_file in source.files:
      # /package_output_path/out__files__/target_location/file
      target_file = ctx.new_file(
          ctx.configuration.bin_dir,
          temp_ramdisk_loc + "/" + source.directory + "/" + source_file.basename)

      temp_outs.append(target_file)
      ctx.action(
        outputs = [ target_file ],
        inputs = [ source_file ],
        command = "mkdir -p '%s'\ncp '%s' '%s'" %
            ( target_file.dirname, source_file.path, target_file.path),
        mnemonic = "COPY",
        progress_message = "Copying file " + source_file.short_path,
      )

  #
  # Create target ramdisk.
  #
  ramdisk_file = ctx.new_file(ctx.configuration.bin_dir, ctx.attr.out)
  ctx.action(
      outputs = [ ramdisk_file ],
      inputs = temp_outs,
      command = "( cd %s; find . | cpio -H newc -o | gzip -9v - ) > %s" %
          ( ramdisk_file.path + RAMDISK_GEN_SUFFIX, ramdisk_file.path ),
      mnemonic = "CPIO",
      progress_message = "Creating ramdisk " + ramdisk_file.path,
  )

  #
  # This is what we produce.
  #
  return struct(files=set([ramdisk_file]))


###############################################################################
#
# Rules accessible by BUILD files
#
###############################################################################

ramdisk_files = rule(
    implementation=_ramdisk_files,
    attrs={
        "srcs": attr.label_list(allow_files=True),
        "out":  attr.string(mandatory=True),
    },
)

ramdisk_create = rule(
    implementation=_ramdisk_create,
    attrs={
        "srcs": attr.label_list(allow_rules=["ramdisk_files"]),
        "out": attr.string(mandatory=True),
    },
)
