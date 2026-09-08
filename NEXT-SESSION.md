# Next session brief — the room authoring tool is ported and working

> Written 2026-09-08, end of the session that executed steps 1–6 of the previous brief.
> Everything below was **verified in this project**, not carried over from Level_Creator_1.
> Where something is unverified, it says so. The brief this replaces is in git history at
> `9317863`; several of its claims turned out to be wrong and are corrected under
> **Corrections** below.

## Where things stand

Branch `port/room-authoring`, 16 commits, tree clean. **Steps 1–6 done. Step 7 remains.**

An authored room now bakes straight into v1's `Master_Room` contract and the pieces drop into
the existing Blueprint generator with no converter. That was the point of the whole port, and
it is done: a real bake ran from the widget on 2026-09-08 and produced four correct pieces.

**62 automation tests, 62 passing, 0 failing.**

| Group | n | What it covers |
|---|---|---|
| `Tools` | 13 | display names, the interior arch, chamber edit/attach, save/load, kit-set override panel |
| `Openings` | 7 | corner rules, one-opening-per-side, Open/Merged spans |
| `KitSet` | 6 | census defaults, inherit, override, ceiling-empty, the null-kit fallback |
| `Attachment` | 5 | centring, normalisation, ascending parents, back sides |
| `Bake` | 5 | all five refusal paths |
| `Layout` | 5 | bounds, overlap, root-cause ordering |
| `Recipe` | 4 | reflection→pure widening, connection toggles |
| `SharedEdge` | 4 | face-to-face, corner-only, diagonal, partial overlap |
| `BakeFrame` | 3 | rebasing against a measured room, rigidity, every entrance |
| `Bounds` | 3 | shrink-wrap, origin normalisation |
| `Contract` | 3 | Master_Room's shape, hand-built rooms, **rooms the bake wrote** |
| `Exits` | 3 | midpoint reachability, planner agreement |
| `Content` | 1 | every migrated asset loads |

---

## How to run things

**Build** (editor must be CLOSED — Live Coding blocks it otherwise):

```bash
"E:/UE4 Projects/_UE4/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
  Procedural_DongeonEditor Win64 Development \
  -project="E:/UE5 Projects/UE5_Procedural_Level_Generator/Procedural_Dongeon.uproject" -waitmutex
```

`Build.bat` does NOT work — the engine path contains a space and the batch wrapper mangles it.
Call `UnrealBuildTool.exe` directly.

**Run tests headlessly:**

```bash
"E:/UE4 Projects/_UE4/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:/UE5 Projects/UE5_Procedural_Level_Generator/Procedural_Dongeon.uproject" \
  -nullrhi -unattended -nosplash -nopause -NoSound \
  -ExecCmds="Automation RunTests ProceduralDungeon.RoomAuthoring;Quit" \
  -TestExit="Automation Test Queue Empty" -log -abslog=<path>
```

Then count `Result={Success}` / `Result={Fail}` in the log. **Do not use the Game target** — it
links but crashes in `BufferReader.h` at startup on uncooked content, before the automation
harness loads, which reads as a test failure and is not one.

**Drive the tool from the console** (works in-editor and under `-ExecCmds`):

```
ProceduralDungeon.Author.Generate <recipe object path>
ProceduralDungeon.Author.Bake     <recipe object path>
```

These exist because the widget's Bake button is still wired to the old signature. Regenerate
must run before Bake — the bake harvests what is standing in the level.

**Live Coding through MCP**, which is how four fixes landed tonight without closing the editor:

```bash
mcp.sh call_tool '{"toolset_name":"LiveCodingToolset.LiveCodingToolset",
                   "tool_name":"CompileLiveCoding","arguments":{}}'
```

It patched a new test file, edits to existing files, and a header change. It **refused** a
struct-layout change and a new `UCLASS`. When it refuses, a real build with the editor closed is
the only way — and note its failure message says nothing useful, so check the actual compile by
building.

---

## MCP — read this before trying to use it

**The external `mcp-unreal` bridge is gone. Do not resurrect it.** `.mcp.json` now points at
Epic's own in-editor server:

```json
{ "mcpServers": { "unreal": { "type": "http", "url": "http://localhost:8000/mcp" } } }
```

UE 5.8 ships `ModelContextProtocol` — "Anthropic MCP server implementation for Unreal Engine",
by Epic. It is already enabled in this project with `bAutoStartServer=True`, port 8000, path
`/mcp`, in `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`. The editor serves it
itself; there is no separate process and nothing to install.

**Why the old one failed, so nobody diagnoses it a third time:** its docs index is a boltdb store
taking an exclusive file lock, and a leftover `mcp-unreal.exe` held that lock across sessions.
Every new spawn blocked before it could answer `initialize`, giving a 30 s handshake timeout. It
was never about the editor being open or closed — both previous sessions blamed that.

**Dispatch:** `bEnableToolSearch=True`, so `tools/list` returns only `list_toolsets`,
`describe_toolset` and `call_tool`. Everything goes through `call_tool` with `toolset_name` +
`tool_name` + `arguments`. Calling a toolset tool directly by its full name fails with
"Unknown tool".

21 toolsets. The useful ones here: `AutomationTestToolset` (run tests in the live editor),
`LiveCodingToolset`, `UMGToolSet` (step 7), `PCGToolset`, `EditorToolset`, `SlateInspectorToolset`.

**Gotchas:**
- `ListTests` requires `tagFilter` even though the schema calls it optional. Pass `""`.
- `RunTestsByFilter` with `ProceduralDungeon.RoomAuthoring` enabled **8855** tests — the whole
  engine suite. Use `ListTests` then `RunTests` with explicit names.
- `GetTestResults` returns only errors and warnings. `AddInfo` output goes to
  `Saved/Logs/Procedural_Dongeon.log` — grep `LogAutomationController` there.
- There is **no Blueprint-inspection toolset**. To read a Blueprint's structure, write an
  automation test that loads it and dumps what you need. That is how the whole `Master_Room`
  contract was established, and `MasterRoomContractTests.cpp` is the working example.

A session started before `.mcp.json` changed keeps retrying the dead stdio server. Restart Claude
Code to pick it up — or drive the HTTP endpoint with `curl` directly, which is what this session
did.

---

## Corrections to the previous brief

Each of these was verified against the assets or the code, and each cost time to discover.

1. **v1's pack is NOT a strict subset of v2's.** v1 has 11 `effects/PS_FX_*` Cascade systems at
   paths v2 lacks. v2's copy is a newer pack version that converted them to Niagara and moved the
   originals to `effects/deprecated/`. The subset claim holds for `comps/` only. This is why a
   pack reimport would have been destructive rather than additive.

2. **The kit needs 15 assets, not 14.** `BP_LVL_01_O_stairs_loop_straight_walled_L1` is a hard
   dependency of `BP_COMP_stairs_straight_01_base__01`.

3. **`ExitCentreUU`, `ExitHalfWidthUU`, `ExitSpanLoUU`, `ExitSpanHiUU` live in `RectRoomPlan.h`,
   not `RectRoomTypes.h`.** `RectRoomPlan` was on the "stays in v2" list. They were gathered into
   this project's subset header instead; their bodies are unchanged.

4. **`RectGen::PlanRoom` appears only in a test comment.** The solver genuinely does not come
   across.

5. **`DefaultEngine.ini`'s `ActiveGameNameRedirects` target the MODULE package path**
   (`/Script/<Module>`), so the rename touched them. Not in the old file list.

6. **`RoomRecipeAsset.h`'s own comments carried the sealed-doorway defect.** They mapped
   `DoorWide = BP_COMP_Door_Walled_01_large`, but that asset is the *sealed filler*; using it as a
   passable door is what broke v2 on 2026-08-28. Three of that comment's four mappings were wrong
   against the census. Corrected in place.

7. **Step 7's trap notes are void.** All the guidance about `read_graph_dsl` not re-parsing,
   `find_node_types`, `get_node_type_pins`, `Behavior|SetValue` describes the *external* bridge,
   which is gone. Epic's `UMGToolSet` is a different surface and **has not been explored**.

8. **Step 3 was incomplete when first reported done.** Only the art assets moved; the emitter,
   the authoring level, the piece tables and the widget did not, so the tool could not run at all.
   Fixed in `4aba973`. Steps 4–6 passed throughout because their tests never touch that content.

---

## Facts measured this session

**`Master_Room`'s structure**, loaded from the asset:

```
DefaultSceneRoot     SceneComponent   (0,0,0)
├── Arrow            ArrowComponent   (1000,0,0)
├── GeometryFolder   SceneComponent   (0,0,0)
├── Overlap Folder   SceneComponent   (0,0,0)
│   └── Overlap_Box  BoxComponent     (1000,0,-50) extent 32, scale 30×30×1
├── Exits Folder     SceneComponent   (0,0,0)
└── FloorSpawnPoints SceneComponent   (0,0,0)
```

`Overlap_Box` is `objectType 14` = `ECC_GameTraceChannel1` = **RoomOverlap, confirmed**. At
extent 32 and Z −50 it is a thin footprint slab *under* the floor, not a room-filling volume.

**How a child room extends it** — added components are roots of their own SCS and name their real
parent through `ParentComponentOrVariableName`. Miss that and everything lands at the actor root,
where the generator's exit scan does not look.

```
exit arrows  → Exits Folder     (inherited)    entrance arrow at (0,0,0)
spawnpoints  → FloorSpawnPoints (inherited)
floorPieces  → GeometryFolder   (inherited)
```

Both inherited components are **overridden per room**, in the `InheritableComponentHandler`, not
the child's SCS:

```
Arrow        → room centre,   (2000,0,0) on a 4000-long room
Overlap_Box  → (centre, 0, -50), scale floor(half/32) − 1
```

That scale rule was derived, not guessed: `1_Room1` ships 61 where 2000 wants 62.5, `1_Hall1`
ships 30 where 1000 wants 31.25, and the bake's own 7×7 room produced 42 where 1400 wants 43.75.
All three fit. A constant inset fits none of them.

**Ceilings.** No room in this project has one — not `Master_Room`, not any of the three
hand-built pieces. But `DA_PieceTable_Ceiling` *does* offer three meshes
(`MOD_Floor_01_O_straight_med` and `_hole`, flipped floors). So `UDungeonKitSet::CeilingMesh`
ships empty because no room is evidence for a default, **not** because there is nothing to pick.

**Wall families diverge.** The hand-built rooms use `MOD_Wall_01_M_*` (pivotMiddle) meshes with
`MOD_Floor_01_E_*` floors. The ported kit uses `BP_COMP_Wall_01_E_*` actors with an `_O_` floor.
Different construction systems, and the census proved `_E_` is the only family that can close a
room — but **authored rooms will not look like the existing ones.** Unresolved by design.

---

## Step 7 — the remaining work

Port `EUW_RoomAuthor` and rewire it. It is already migrated to
`/Game/RectDungeon/Authoring/EUW_RoomAuthor` and it runs; two things are wrong with it.

**1. The Bake button's saved-path pin is stale.** `BakeAuthoredRoom` now returns
`TArray<FString>& OutSavedPaths` rather than a single `FString`, because it writes one piece per
exit. The status line comes through correctly — the refusal seen on the first attempt was this
project's own message — but anything the widget says about *where* it wrote is not trustworthy.
Read the Output Log's `wrote ...` lines instead until this is fixed.

**2. The piece panel cannot express "inherit".** A chamber's piece slots are now OVERRIDES, and
empty means inherit from the kit set. `GetChamberPieces` deliberately returns the raw slot, so an
un-overridden chamber reads back empty — that is correct and is tested
(`Tools.ThePiecePanelShowsOverridesNotInheritedValues`). The widget needs to *show* that
distinction, and needs a `KitSet` field on the room panel. **Do not make the getter resolve:** a
Get/Set round trip would then freeze every inherited value into an explicit override and the room
would stop following its kit, which is the exact duplication kit sets remove.

---

## Backlog, roughly prioritised

- **Geometry volume.** 966 SCS nodes and 1.1 MB *per baked piece*, ×4 pieces for a four-exit
  room. Blueprints get slow to open and compile around ~1000 SCS nodes, and this is per room in
  the library. If it bites, the fix is instanced static meshes rather than one component per
  tile — a real change, not a tweak.
- **Floor spawn points include the perimeter.** 49 points for a 7×7 room is every tile; the
  original brief said *interior* tiles. A point therefore sits against each wall. One-line fix to
  inset by a tile if wanted.
- `Config/DefaultGame.ini` still says `ProjectName=Third Person BP Game Template`.
- **The 5.7→5.8 content resave** is still outstanding and now *partial* — `1_Hall1` and two
  ThirdPersonMap actors were resaved incidentally (`03a7a0d`). Doing the rest deliberately, in
  its own commit, is still the right shape.
- **The `.uproject` is LFS-tracked** (`*.uproject filter=lfs` in `.gitattributes`). A clone
  without LFS installed gets a pointer file and cannot open the project.
- Untracked throwaways from tonight's testing: `Content/RectDungeon/Rooms/Generic/` — one recipe
  and four baked rooms. Delete or commit.
- `magic` MCP server fails to connect (API key reset). Unrelated to this work.

---

## Traps

- **Live Coding is not a build.** It patched four changes tonight and refused two. When it
  refuses, its message is useless — build properly to see the real error. And a "successful"
  headless test run right after a *failed* build is running the **stale DLL**; it means nothing.
  This happened once tonight and nearly reported a false pass.
- **The editor must be closed to build.** Every step needing a rebuild costs a close/open cycle
  unless Live Coding can take it. The natural rhythm is: leave the editor closed while working,
  open it to test.
- **The bake's success path has no automated test** and cannot easily have one — it needs a live
  authoring world with generated actors, and a test that writes into the room library then fails
  mid-run is how a throwaway asset gets committed. `Contract.BakedRoomsSatisfyTheMasterRoomContract`
  covers the next best thing: it checks whatever rooms are *in* the library, so run a bake by
  hand and then run that test.
- **Every bake-refusal fixture carries a chamber-free backstop** so no test can drive a real bake
  even if the guard it exercises stops firing. Keep that when adding fixtures. It also means
  guard ordering matters: the no-exits guard was unreachable until it moved above
  `ValidateRecipe`, because the backstop refused first.

---

## Reference

| Document | What it carries |
|---|---|
| `HANDOFF.md` | v1's inventory, the room contract, §3's ranked design problems |
| git log `port/room-authoring` | every decision, with its reasoning, in the commit messages |
| `Source/ProceduralDungeon/RoomAuthoring/Tests/MasterRoomContractTests.cpp` | how to read a Blueprint's structure headlessly |
| v2 `docs/superpowers/specs/2026-09-07-kit-sets-design.md` | the kit-set design and the Phase 0 census |
| v2 `production/session-state/active.md` | 6,397 lines of session history — search it, don't read it |
