# AGENTS.md

Conventions for automated and human contributors to this fork.

## Attribution in comments

`Jesse` is the upstream maintainer of `scallyw4g/bonsai`, `scallyw4g/bonsai_stdlib`
and `scallyw4g/bonsai_debug`. **Never** put `Jesse` in a comment, `TODO`, `NOTE`
or `FIXME` that you author.

Use your own handle instead:

```c
// NOTE(nsillik): ...
// TODO(nsillik): ...
// TODO(nsillik)(macos): ...
```

Existing `TODO(Jesse)` / `NOTE(Jesse)` markers are upstream's. **Leave them
alone.** Do not renumber, reword, reformat or "fix" them — upstream uses
`TODO(<name> id: 123)` with stable ids that are cross-referenced elsewhere.

When editing a line that carries an upstream marker, keep the marker intact
unless the change is actually yours, in which case leave the upstream marker and
add your own on a separate line.

Beware of unanchored `sed`: `sed 's/NOTE(Jesse)/NOTE(nsillik)/'` across a tree
rewrites upstream comments too. Match on the full line including its comment
prefix, and diff the result before committing.

## Do not open PRs against upstream

This fork is for local development. Work lands on branches in
`nsillik/bonsai`, `nsillik/bonsai_stdlib` and `nsillik/bonsai_debug`.

**Never** open a PR, issue, or comment on a repository owned by someone else,
including `scallyw4g/*`. That includes drafts. If a change belongs upstream,
leave it on a fork branch and say so — the maintainer can pull it.

The submodules use relative URLs (`../bonsai_stdlib.git`), which resolve against
the fork, so `git submodule update --init --recursive` needs the fork copies to
exist. They do.

## Stacked PRs

Branches chain bottom-up: `master` ← `port/macos` ← `port/macos-phase1`. Open
each PR against its **parent branch**, not `master`:

```bash
gh pr create -R nsillik/bonsai -H port/macos-phase1 -B port/macos
```

Never retarget a stacked PR to `master` while its parent is still unmerged.
Merge bottom-up.

`gh` needs a repository hint on every command. `branch.master.remote` and the
fork layout confuse auto-detection, so pass `-R nsillik/bonsai` (or set
`gh repo set-default nsillik/bonsai` once per clone).

**Do not use the `gh stack` CLI.** It resolves the repository to the *parent* of
the fork — `gh stack init` writes `github.com:scallyw4g/bonsai` into
`.git/gh-stack` regardless of `gh repo set-default` — so its PR lookups fail
(`Could not resolve to a PullRequest with the number of 1`) and `gh stack submit`
attempts to create PRs on upstream. That directly conflicts with the rule above.

Grouping the PRs into a native Stack **from the GitHub web UI does work** — that
is how the `port/macos` stack is linked. It is a manual step; do it after the PRs
exist. Confirm with:

```bash
gh api graphql -f query='{ repository(owner:"nsillik", name:"bonsai") {
  pullRequest(number:2) { stackEntry { position stack { number } } } } }'
```

## Build

`./make.sh` is the entry point. `./make.sh RunTests` runs the suites.

macOS builds are **cross-targeted to x86_64** and run under Rosetta 2, because
the SIMD layer is SSE/AVX-only (`-mssse3 -mavx -mavx2 -mfma` are hard errors for
an `arm64-apple-darwin` target). Consequently:

- `.github/workflows/build.yml`'s `build-macos` job must install Rosetta before
  it runs any built binary.
- Anything that inspects `ARCH` gets the *target* arch, not the host arch.
- Rosetta reports `_SC_PAGESIZE == 4096`, not the host's 16384.

Every file in the tree is compiled as Objective-C++ on macOS (`-x objective-c++`
in `platform/macos` builds), because the engine is one translation unit per
target and `macos_platform.cpp` uses AppKit directly. There is no `.mm` shim.

## Platform code

Platform-specific code lives in `external/bonsai_stdlib/src/platform/<os>/`, and
is pulled in by `src/platform.h` (declarations) and `src/platform.cpp`
(definitions). Adding a platform means editing **both** ladders in
`platform.cpp` — the `posix_platform.cpp` guard at the top is separate from the
per-OS ladder below it.

`posix_platform.{h,cpp}` is shared by Linux, macOS and EMCC. Anything declared
there must not depend on Linux-only APIs.
