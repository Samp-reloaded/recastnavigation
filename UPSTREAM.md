# Recast Navigation C++23 fork

This repository is an altered fork of Recast Navigation, maintained by Samp-reloaded.
The upstream source history and license notices are preserved.

- Upstream: https://github.com/recastnavigation/recastnavigation
- Base revision: `9f4ce64458dfae86e1239c525ddc219c4e9e06f1`
- Modernization branch: `codex/cpp23-navigation`
- Native API baseline: C++23, named modules, explicit ownership and checked results.

`Modern/` contains independently compiled native modules. The original source tree
is retained for algorithm comparison; its packed runtime layouts are not the new
document format. Source packages contain modules, not precompiled compiler-specific
BMI files. Runtime, query, tile-cache and crowd cutovers require their own evidence.
