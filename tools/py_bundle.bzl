ZIP_FILE_SOURCE="__files__"
ZIP_FILE_SUFFIX=".zip"

#
# _py_bundle creates an executable shell script that bundles in all
# python files (and eventually resources, too).
#
# Script executes in 5 stages:
# 1) populate all files from dependencies supplied by 'srcs',
# 2) create __init__.py files for every directory that contains python sources,
# 3) create top-level __init__.py file to allow cross-referencing modules,
#    eg. host/ivshmem_server:launcher can reference host/ivshmem_server:ivserver,
# 4) build a zip file,
# 5) craft a script that enables python to use the bundle.
def _py_bundle(ctx):
  # List of files that have to be included in target python bundle.
  temp_outs = list([])
  temp_loc = ctx.attr.name + ZIP_FILE_SOURCE
  # List of all folders containing python code.
  python_dirs = dict()

  #
  # Iterate all dependencies and copy files.
  #
  for source in ctx.attr.srcs:
    for source_file in source.files:
      # When populating python files, create __init__.py file for
      # every directory containing python code (and its parent).
      if source_file.basename.endswith('.py'):
        package = source_file.dirname.split('/')
        for component in reversed(range(len(package))):
          path = "/".join(package)
          # Update with default (False) or current (if __init__.py exists).
          python_dirs[path] = python_dirs.get(path, False)
          package.pop(component)

        # Don't create python __init__.py file if we already have one.
        if source_file.basename == "__init__.py":
          python_dirs[source_file.dirname] = True

      # Copy python files.
      target_file = ctx.new_file(
          ctx.bin_dir,
          temp_loc + "/" + source_file.dirname + "/" + source_file.basename)

      temp_outs.append(target_file)
      ctx.action(
        outputs = [ target_file ],
        inputs = [ source_file ],
        command = "mkdir -p '{}'; cp '{}' '{}'".format(
            target_file.dirname,
            source_file.path, target_file.path),
      )

  #
  # Create missing __init__.py files for all folders with python code.
  #
  for (folder, hasinit) in python_dirs.items():
    if hasinit:
      continue
    init_file = ctx.new_file(
        ctx.bin_dir, "{}/{}/__init__.py".format(temp_loc, folder))
    temp_outs.append(init_file)
    ctx.action(
        outputs = [ init_file ],
        command = "touch '{}'".format(init_file.path))


  #
  # Create init file and instruct it to launch 'main()' method
  # of the specified import.
  #
  main_file = ctx.new_file(
      ctx.bin_dir, temp_loc + "/__main__.py")
  ctx.action(
      outputs = [ main_file ],
      command = "( echo 'import {0}\n{0}.main()' ) > {1}".format(
          ctx.attr.main, main_file.path)
  )
  temp_outs.append(main_file)


  #
  # Create zip file.
  #
  zip_file = ctx.new_file(ctx.bin_dir, ctx.attr.name + ZIP_FILE_SUFFIX)
  ctx.action(
      outputs = [ zip_file ],
      inputs = temp_outs,
      command = "( cd '{0}/{1}'; zip -r - . ) > '{2}'".format(
          zip_file.dirname, temp_loc, zip_file.path)
  )


  #
  # Create shell executable
  #
  out_file = ctx.new_file(ctx.bin_dir, ctx.attr.name)
  ctx.action(
      outputs = [ out_file ],
      inputs = [ zip_file ],
      command = ("( " +
          "echo '#!/bin/sh';" +
          "echo 'PYOUT=$(mktemp -t {2}-XXXXXX)';" +
          "echo 'trap \"rm $PYOUT\" INT KILL EXIT';" +
          "echo 'uudecode >$PYOUT <<\"EOF\"';" +
          "uuencode {1} -;" +
          "echo 'EOF';" +
          "echo 'python $PYOUT \"$@\"') > {0}").format(
              out_file.path, zip_file.path, ctx.attr.main))

  #
  # This is what we produce.
  #
  return struct(files=set([out_file]))


###############################################################################
#
# Rules accessible by BUILD files
#
###############################################################################

py_bundle = rule(
    implementation=_py_bundle,
    executable=True,
    attrs={
        "srcs": attr.label_list(allow_rules=["py_library"]),
        "main": attr.string(mandatory=True),
    },
)
