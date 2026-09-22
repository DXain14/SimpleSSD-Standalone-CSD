# Notices

## Upstream SimpleSSD Standalone

This repository is based on the public SimpleSSD Standalone codebase:

- Project: [SimpleSSD Standalone](https://github.com/simplessd/simplessd-standalone)
- Upstream branch: `2.0`
- Local base commit: `8255367ff8736f3dce16612054114a3ffed5e7ab`
- License: GNU GPLv3
- Original copyright notices: CAMELab and SimpleSSD

## Embedded SimpleSSD

The `simplessd/` directory is a Git submodule. Its current local source tree
is content-identical to the CSD-modified SimpleSSD tree, but its Gitlink must
be updated to a public CSD commit before release.

- Current local submodule base: `73ad8ad5220ce5ebfeb055687d0328765a046c0f`
- License: GNU GPLv3

## CSD extension

The CSD extension and standalone workload changes are listed in
[`CSD_MODIFICATIONS.md`](CSD_MODIFICATIONS.md). The release owner must record
the legally authorized CSD copyright holder or contributors before public
distribution.

Copyright (C) 2026 DXain14

`DXain14` is the public GitHub identifier of the CSD release owner. No agent
or automated coding tool is a copyright holder or contributor.

## Third-party libraries

- `lib/drampower`: retain its bundled README, copyright, and license notices.
- `simplessd/lib/inih`: retain `simplessd/lib/inih/LICENSE.txt`.
- `simplessd/lib/mcpat`: retain its bundled source notices, README, and
  submodule history.

The applicable project license is the GPLv3 text in `LICENSE`.
