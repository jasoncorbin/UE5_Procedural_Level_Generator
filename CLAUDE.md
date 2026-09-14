# Procedural Dungeon Generator

UE 5.8. C++ module `ProceduralDungeon` + Blueprint. A room-authoring tool bakes authored rooms
into v1's `Master_Room` contract; a Blueprint generator assembles them at runtime.

## Do not

- Do not use `Build.bat`. The engine path contains a space and the wrapper mangles it. Call `UnrealBuildTool.exe` directly.
- Do not build with the editor open. Live Coding holds the DLL.
- Do not run automation against the Game target. It crashes in `BufferReader.h` before the harness loads, which reads as a test failure and is not one. Use the Editor target.
- Do not trust Live Coding as a build, and do not trust a headless run that follows a failed build — it executes the stale DLL.
- Do not resurrect `mcp-unreal`, the third-party Go bridge (`github.com/remiphilippe/mcp-unreal`, targets UE 5.7). Epic's own `ModelContextProtocol` plugin is what `.mcp.json` registers, under the name `unreal`.
- Do not rewrite `EUW_RoomAuthor`'s EventGraph with `write_graph_dsl`. It carries designer-bound button events the writer cannot reproduce; the rewrite silently unbinds Save, Load and Bake.
- Do not write a node type id you read back from `read_graph_dsl` without verifying it. Ids are ambiguous and the wrong one builds silently.
- Do not widen the baked-room ignore rule to the whole folder. `DA_RoomRecipe_*` lives there and is authored source.
- Do not commit without reading `git status`. Editor autosave writes assets mid-commit; three accidental bakes have landed that way.

Symptoms and alternatives for each: `failures.md`.

## State

- Working: the room-authoring tool is ported and wired. 69/69 automation tests pass. An authored room bakes into `Master_Room` with no converter. One branch, `master`, since PR #1 merged.
- In progress: nothing. The port is complete.
- Next: open the tool and click through it. Nothing was verified visually — check that piece combos read `(inherit - <piece>)` on an un-overridden chamber, the KitSet row appears under RoomType, and the bounding spins survive a Refresh instead of collapsing to 1.

## Files

- `failures.md` : every known trap, its symptom, and what to do instead. Read before driving MCP or committing.
- `build-and-test.md` : how to build, run the suite headlessly, and drive the tool. Read when running anything.
- `mcp.md` : Epic's in-editor MCP surface and its dispatch rules. Read before any MCP call.
- `room-contract.md` : what `Master_Room` requires and how the bake satisfies it. Read when touching rooms, pieces or kits.
- `generator-port.md` : the runtime generator's unfixed design flaws and the Unity solutions to port. Read when starting generator work.
- `backlog.md` : known-outstanding work nobody is doing. Read when picking up something new.
- `HANDOFF.old.md` : pre-port history, kept as written. Not in the read path; several claims are stale.
