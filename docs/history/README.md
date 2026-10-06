# History: how the first port was done

These twelve chapters are the build journal of the Trix port, written while it happened. They
explain *why* things are the way they are, with the commands that were run by hand before
`make setup` and `make new` automated them. Paths in them (`game/`, `trix-bin`, `host.cpp`)
are from before the repository was reorganised.

For current, maintained information use the [guides](../guides/) and [reference](../reference/).
Where the two disagree, the reference wins.

| # | Chapter | Still the best place for |
|---|---------|--------------------------|
| 01 | [Platform anatomy](01-platform-anatomy.md) | How the cabinet boots and runs a game |
| 02 | [Toolchain setup](02-toolchain-setup.md) | What `setup.sh` does step by step and why (32-bit gcc without root, private apt, Ghidra) |
| 03 | [Extracting a game](03-extracting-a-game.md) | Dependency closure, which 2008 libraries must come from the image |
| 04 | [Mapping the runtime](04-mapping-the-runtime.md) | How the seam (`CreateNewGameDevice`) was found |
| 05 | [Reverse engineering](05-reverse-engineering.md) | Method for recovering layouts (now in [reference/engine-abi.md](../reference/engine-abi.md)) |
| 06 | [Writing the backend](06-writing-the-backend.md) | The vtable-cloning technique, explained from scratch |
| 07 | [Host and data](07-host-and-data.md) | Why the filesystem shim and old-ABI `stat` exist |
| 08 | [Asset formats](08-asset-formats.md) | How `.spr` was decoded |
| 09 | [Debugging and profiling](09-debugging-and-profiling.md) | How the profiler works inside |
| 10 | [Bug catalogue](10-bug-catalogue.md) | The first 20 bugs in the order they were hit |
| 11 | [Porting another game](11-porting-another-game.md) | Early plans for the Unity, Merit3D and legacy families |
| 12 | [Scaffolding](12-scaffolding.md) | First description of the template |
