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

## Implemented module archives

`Modern/CMakeLists.txt` owns seven source-built static components: `RecastCore`,
`RecastCodec`, `DetourMesh`, `DetourQuery`, `RecastBuild`, `DetourTileCache` and
`DetourCrowd`. Queries retain immutable mesh snapshots. The cooker uses the
upstream Recast kernel through private scoped allocation. Tile publication is an
atomic affected-tile batch; callers still supply cooked replacements. Crowd emits
steering intents and commits externally resolved motion; it does not own physics
or execute off-mesh actions.

## Native checkpoint, 2026-10-13

The Windows x64 VS18/v145 RelWithDebInfo integrated source build compiled and
linked all seven components at source revision `60bd898`. Its navigation test
executable passed 65 test cases and 1,872 assertions; the registered navigation
CTest suite passed. This verifies the fork's component checkpoint, not complete
RW4/game navigation activation, streaming, world cooking or gameplay physics.
Android, other compilers, runtime performance and upstream differential corpora
have not been accepted by this checkpoint.
