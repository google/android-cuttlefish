# rules_rust patches

`rules_rust.MODULE.bazel` applies the patches in this directory to `rules_rust` through a
`single_version_override`. They are meant to be temporary: each one should be
upstreamed and dropped once a release containing it is picked up.

## 0001-crate_universe-share-git-checkouts.patch

**Problem.** `crate_universe` turns every crate it resolves into its own Bazel
repository. For crates that come from git this means one `new_git_repository`
per crate, even when many crates come from the same remote at the same commit.
crosvm is a Cargo workspace with ~70 member crates, so a cold build cloned
`https://chromium.googlesource.com/crosvm/crosvm` ~70 times (each with
submodules), which trips googlesource's rate limiting (`HTTP 429`) and adds
minutes of fetch time. See b/574521063.

**Change.** The `crate` module extension now creates a single
`new_git_repository` per `(remote, commit)` and a small
`crate_from_git_source` repository per crate that symlinks the crate's
subdirectory out of that shared checkout. Crates with `patches` get a private
copy instead of symlinks so the shared checkout stays pristine. The generated
`BUILD` files, repository names and feature resolution are unchanged.

Before:

```
_crate_impl
└─ _generate_hub_and_spokes(crosvm_bin)
   └─ for crate in lock["crates"]:            # 68x for crosvm
      └─ "Git" → new_git_repository(crosvm_bin__<crate>, remote, commit, strip_prefix=<crate>)
                 └─ [at fetch] git init / git fetch <remote> / git submodule update   # 68 clones
```

After:

```
_crate_impl
└─ _generate_hub_and_spokes(crosvm_bin)
   └─ for crate in lock["crates"]:            # 68x for crosvm
      └─ "Git" → _git_source_repo_name(remote, commit)
                 ├─ first time only → new_git_repository(crosvm_bin__git_<remote>_<commit>)
                 │                    └─ [at fetch] git init / git fetch / git submodule update   # 1 clone
                 └─ every time      → crate_from_git_source(crosvm_bin__<crate>, source=@shared, strip_prefix=<crate>)
                                      └─ [at fetch] symlink <shared>/<crate>/* into the crate repo   # no git
```

**Updating.** When bumping `rules_rust`, re-apply the patch on top of the new
`crate_universe/extensions.bzl`; the touched block is small and has not moved
in recent releases. If the upstream change has landed, delete this patch, the
`single_version_override` in `MODULE.bazel` and this section.
