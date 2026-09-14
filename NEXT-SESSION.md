# Next session brief — the room authoring tool is ported, wired and working

> Written 2026-09-08 at the end of steps 1–6; **revised 2026-09-09 at the end of step 7**,
> which finished the port; **revised again 2026-09-12**, when the branch was pushed to
> `origin` and four backlog items closed.
>
> Everything below was **verified in this project**, not carried over from Level_Creator_1.
> Where something is unverified, it says so — and the largest unverified thing is named at
> the top of **Start here**, because it is the one gap worth closing first.
>
> This brief has now corrected its own predecessor twice. The brief it replaced is at
> `9317863`; corrections 1–8 are against that one, and **corrections 9–12 are against the
> 2026-09-08 revision of this file** — several of its confident claims about the widget
> turned out to be wrong, including one that had the Bake button working when it could not
> have been. Treat the Corrections section as the most load-bearing part of the document.

## Start here

1. **Open the tool and look at it.** Nothing in step 7 was verified by clicking — every
   graph was read back and the widget compiles clean with warnings as errors, but no human
   has seen the panel since it changed. Specifically worth checking: the piece combos read
   `(inherit - <piece>)` on an un-overridden chamber, the new KitSet row appears under
   RoomType, and the bounding spins survive a Refresh instead of collapsing to 1.
2. **Run a bake from the Bake button**, not the console. It should work now for the first
   time — see correction 12 — and `Contract.BakedRoomsSatisfyTheMasterRoomContract` checks
   whatever the library holds, so run it afterwards.
3. **Author a `DA_KitSet_*` asset** if you want the kit picker to do anything. It offers
   exactly one entry today, correctly, because the project has no kit set assets.

Everything else is in **Backlog**, and none of it blocks.

## Where things stand

Branch `port/room-authoring`, 32 commits, **pushed to `origin` 2026-09-12**. **Steps 1–7
done.** The port is complete; what remains is content and one visual pass, both listed under
**Step 7**.

An authored room now bakes straight into v1's `Master_Room` contract and the pieces drop into
the existing Blueprint generator with no converter. That was the point of the whole port, and
it is done: a real bake ran on 2026-09-08 and produced four correct pieces. (That bake was
almost certainly driven by the console command rather than the widget button -- see
correction 12.)

**69 automation tests, 69 passing, 0 failing** — verified headlessly against a real build,
not a Live Coding patch.

| Group | n | What it covers |
|---|---|---|
| `Tools` | 20 | display names, the interior arch, chamber edit/attach, save/load, kit-set override panel, the inherit vocabulary, the kit picker |
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


9. **The Bake button's saved-path pin was NOT stale.** Step 7's problem 1 was void. The
   `BakeAuthoredRoom` node in `DoBake` already carried the current signature's
   `OutSavedPaths` array pin, `OutStatus` was wired to the status line, and the widget
   compiled clean with warnings as errors. Blueprint call nodes reconstruct against the
   current `UFUNCTION` on load, so the mismatch healed itself. The status string already
   names the folder and the piece count; only the per-piece paths are absent, which is
   cosmetic. **Nothing was wrong with the Bake button.**

10. **Live Coding accepts new `UFUNCTION`s.** The previous brief expected a refusal. It
    patched three new reflected static functions across four compiles without complaint.
    Its real limit is elsewhere — see the traps.

11. **Epic's toolset has a full Blueprint graph surface**, not just UMG. Step 7's note that
    `UMGToolSet` "has not been explored" is answered: `UMGToolSet` manipulates the widget
    TREE only, and every graph edit in this session went through
    `editor_toolset.toolsets.blueprint.BlueprintTools` instead — `read_graph_dsl`,
    `write_graph_dsl`, `find_node_types`, `get_node_type_pins`, `add_function_param`,
    `compile_blueprint`. The old external bridge's tool names survive here because they were
    always Epic's; the previous brief's guess that they were the bridge's own was wrong.

12. **`PushUI` was pushing a 1x1 bounding rect, and that is why the Bake button never worked.**
    Its `SetRoomFields` node had `BoundingWidth` and `BoundingLength` unconnected at literal
    `0`, while every other data pin was wired; `SetRoomFields` clamps with `Max(1, ...)`. The
    widget never calls `FitBoundsToChambers`, so nothing put the bounds back. A 7x7 chamber in
    a 1x1 rect fails validation, so `DoBake` -- which calls `PushUI` first -- would have
    refused.

    This contradicts the previous brief's "a real bake ran from the widget". What can be said
    for certain is only that the pins were disconnected on 2026-09-09 and that a widget bake
    cannot succeed in that state; whether they were disconnected on the 8th was not
    determined. The console path (`ProceduralDungeon.Author.Bake`) never touches `PushUI`,
    which fits the evidence and fits why those commands were written at all.

    Fixed by connecting both pins to the spin boxes `PullUI` already fills.

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

## Step 7 — done

`EUW_RoomAuthor` is migrated, rewired and compiling clean with warnings as errors. The piece
panel speaks about inheritance correctly, the room panel has a kit-set picker, and a bug that
made the Bake button unusable is fixed.

**Not verified by clicking.** Every graph was read back after writing, the widget compiles, and
every C++ link is tested — but nobody has opened the tool and looked at it since. That is the
one thing worth doing before building further.

### What was wrong, and what it turned out to be

| Believed | Actually |
|---|---|
| The Bake button's saved-path pin is stale | Void. The node had already reconstructed against the current signature; `OutStatus` was wired and the status string already names the folder and count |
| The piece panel cannot express "inherit" | Understated. It was *destroying* inheritance on every repaint — see below |
| — | `PushUI` passed literal `0` for both bounding dimensions, collapsing every room to 1×1 on every push. This is why the Bake button never worked and the console commands exist |

**The freeze.** `RepaintPanel` ends with `PushChamber`, and `PullChamber` called
`SelectOption(combo, "")` for an inherited slot. `SelectOption` could not find `""`, did
nothing, and left index 0 — a real piece — selected, which `PushChamber` then wrote back as an
explicit override. Opening the tool or loading a room converted all seven inherited slots into
hard overrides.

Fixed by making index 0 not a real piece: `FillCombo` seeds each combo with
`GetInheritedPieceLabel` and takes a `Slot` argument, and `SelectOption` treats an empty option
as "select index 0". `PathForDisplayName` does not recognise the bracketed label, so pushing it
back yields the empty path that means inherit.

### The C++ this rests on

| Function | What it is for |
|---|---|
| `GetInheritedPieceDisplayName(Recipe, Slot)` | what a slot WOULD resolve to. Reads the kit only — never a chamber, so it cannot hand the panel a value to freeze |
| `GetInheritedPieceLabel(Recipe, Slot)` | the combo entry meaning inherit: `(inherit - Wall_01_E_straight_large)` or `(inherit - none)` |
| `SetRoomKitSet(Recipe, KitSet)` | the only door onto `KitSet`, which is `BlueprintReadOnly`. Null is legal |
| `GetKitSetLabels()` | every kit set in `/Game`, built-in census first |
| `KitSetForLabel(Label)` | label → asset; null for the built-in entry and for unknown labels |
| `LabelForRoomKitSet(Recipe)` | asset → label, DERIVED through the same rule the scan uses |

Slot names mirror `GetChamberPieces`' outputs: `Wall`, `CornerNW`, `CornerNE`, `CornerSE`,
`CornerSW`, `Floor`, `CeilingMesh`. All four corners answer with the kit's single `Corner`.

Two bracketed labels — `(inherit - ...)` and `(built-in census)` — carry a load-bearing
guarantee: an asset name cannot contain a bracket, so neither can ever collide with a real
piece or kit however the libraries grow.

### How the panel applies a kit

**On Refresh, not on selection.** `RefreshCombos` fills and selects the picker; `PushUI` reads
it back. That matches the rest of the panel — the piece combos have no change handlers either.
`DoRefresh` now calls `RefreshCombos` so the `(inherit - ...)` labels, which are baked in at
fill time, follow a kit change.

Immediate-apply was attempted and abandoned. UMG widget delegates are not `ActorComponent`
events, so `add_component_bound_event` refuses them, and every alternative means rewriting the
`EventGraph` — which carries designer-bound button events (`OnClicked(SaveButton)` and friends)
that the DSL writer cannot reproduce. **Do not rewrite the EventGraph with `write_graph_dsl`.**

### Left over

- **No `DA_KitSet_*` asset exists yet**, so the picker offers exactly one entry. Authoring the
  first one is a content task, and it is what would make
  `Tools.EveryListedKitSetResolvesAndRoundTrips` do real work — its loop is vacuous today and
  logs that it checked nothing.
- The bake's per-piece paths still are not shown in the widget; `OutSavedPaths` is wired to
  nothing. The status line names the folder and the count, so this is cosmetic.

---

## Backlog, roughly prioritised

- **Two branches, two `.gitignore` files — keep them in step.** Every ignore rule this port
  added lived only on `port/room-authoring`, so `master` never had them. On 2026-09-12 a commit
  on `master` picked up the 8 baked `BP_Room_*` pieces and the per-machine
  `.vscode/compileCommands_*` and pushed them — the **third** accidental commit of derived bake
  output, and each one landed on whichever branch the guard did not reach. `d939a8a` copies the
  block onto `master` verbatim, so the two agree today. **They will drift again the moment a
  rule is added to one branch only.** `transcript.txt` is deliberately left tracked on `master`:
  the rule matches it, but it is authored content, not derived output.

- ~~A GitHub personal access token is embedded in the `origin` remote URL.~~ **Resolved
  2026-09-12.** The remote is now a bare `https://github.com/...` URL with no credentials in
  it, and Git Credential Manager holds the secret. The exposed token is dead — GitHub rejected it
  with *Invalid username or token* — so there is nothing left to rotate.

  Worth knowing for next time: GCM stores the github.com credential under username `jlcorbin`,
  not `jasoncorbin`. When it goes stale every push fails *without prompting*, which reads like
  a network or permission fault and is neither. To see what is stored, feed `protocol=https`
  and `host=github.com` on stdin to `git credential-manager get`.
- ~~Two throwaway recipes are untracked on disk.~~ **Committed 2026-09-12** in `00daced`.
  They are now tracked room source. If they were genuinely throwaway, delete them in their own
  commit rather than leaving them to be mistaken for a library.
- ~~`L_RoomAuthoring.umap` has been modified since before 2026-09-08 and is uncommitted.~~
  **Committed 2026-09-12** in `00daced`, along with everything else that was sitting in the
  tree. Nobody established what the change was, so it is now committed rather than understood.
- **Geometry volume.** 966 SCS nodes and 1.1 MB *per baked piece*, ×4 pieces for a four-exit
  room. Blueprints get slow to open and compile around ~1000 SCS nodes, and this is per room in
  the library. If it bites, the fix is instanced static meshes rather than one component per
  tile — a real change, not a tweak.
- **Floor spawn points include the perimeter.** 49 points for a 7×7 room is every tile; the
  original brief said *interior* tiles. A point therefore sits against each wall. One-line fix to
  inset by a tile if wanted.
- `Config/DefaultGame.ini` still says `ProjectName=Third Person BP Game Template`.
- **The 5.7→5.8 content resave** is still outstanding and still *partial*. `1_Hall1` and two
  ThirdPersonMap actors were resaved incidentally and committed in `03a7a0d`. Doing the rest
  deliberately, in its own commit, is still the right shape.

  The five that were sitting uncommitted on 2026-09-09 — `1_Hall2.uasset` and four
  `__ExternalActors__/ThirdPerson/Maps/ThirdPersonMap/` actors — **were committed 2026-09-12**
  in `00daced`. That does not finish the resave, it just moves the boundary: opening the editor
  still upgrades whatever 5.7 packages it touches, so more will appear. The deliberate pass is
  still owed.
- **The `.uproject` is LFS-tracked** (`*.uproject filter=lfs` in `.gitattributes`). A clone
  without LFS installed gets a pointer file and cannot open the project.
- ~~Decide whether baked rooms belong in git.~~ **Decided 2026-09-09: ignored.** The rule
  now covers `Rooms/**/BP_Room_*` and `Rooms/**/DA_Room_*` — every room type and both
  derived prefixes, where it previously named only `Generic/BP_Room_*`. It deliberately
  does NOT ignore the whole folder: `DA_RoomRecipe_*` sits in the same directory by design
  and is authored SOURCE, so a blanket rule would have quietly stopped tracking the rooms
  themselves. `DA_Room_*` does not match `DA_RoomRecipe_*`.
- `magic` MCP server fails to connect (API key reset). Unrelated to this work.

---

## Traps

**The work is on `port/room-authoring`, and `master` does not have any of it.** Checking out
`master` makes the project look like the port never happened: no `Source/ProceduralDungeon`,
a 53-byte `.uproject` with no modules, none of the authoring content. 106 files differ. This
happened on 2026-09-12 and read convincingly as the project having reverted. It had not.
`git reflog` settles it in one line, and `git switch port/room-authoring` undoes it.

**Close the editor before switching branches.** The switch rewrites `Procedural_Dongeon.uproject`
itself — that is the file declaring the `ProceduralDungeon` module — and adds 61 content assets
while deleting 33. Doing that under a live editor is how a session gets corrupted.

Switching away from `master` also *deletes* 20 files that `master` tracks and the port branch
does not, under `Saved/` and `Intermediate/` — including `Saved/Config/WindowsEditor/`, which
holds editor layout and preferences. They regenerate, but back them up if you care about the
layout. Git refuses the switch outright if any of them are locally modified; stash rather than
discard, because `git checkout -- .` throws away the only copy.

- **THE MYSTERY CONTENT COMMITS ARE EXPLAINED.** An MCP call that touches an asset marks it
  dirty, and the editor writes it out on close or autosave — so a `.uasset` appears modified in
  `git status` some time after the call, with nothing in the session obviously touching content.
  A single `compile_blueprint` on `EUW_RoomAuthor` did it this session, and the change was a
  pure re-serialise with no semantic content. That is the same class of cause as the two
  accidental baked-room commits: content written *while* a commit was being prepared, by the
  editor rather than by the `git add`. **Still check `git status` before every commit** — but
  the mechanism is no longer unknown, and the answer is usually `git checkout --` on an asset
  you did not mean to change.

- **Live Coding registers new automation tests only on a module's FIRST patch per editor
  session.** Four new tests added on the first patch appeared and ran. A fifth added on a later
  patch never appeared: the compile reported `Result: Success`, `ListTests` did not list it, and
  `RunTests` returned `total: 0` rather than an error. Touching the file and recompiling did not
  help. **A test that will not appear is not a passing test** — close the editor and build. The
  same is not true of function bodies or new `UFUNCTION`s, which patch fine.

- **The graph DSL does not round-trip.** `read_graph_dsl` emits forms `write_graph_dsl` refuses:
  - Member-function calls print their arguments without `self`, but the writer binds the first
    positional argument TO `self`. Use keyword form — `(CallFunction|ComboClear :Combo Combo)`,
    not `(CallFunction|ComboClear Combo)`. The reader is not even consistent about this: it
    printed `self` explicitly in `FillAttachCombo` and omitted it in `FillCombo`.
  - Type ids can be ambiguous. The widget's own `SelectOption` reads back as
    `SwitchActor|SelectOption`, which is an unrelated ENGINE function on `ASwitchActor` with
    completely different pins. Writing what you read would have silently built the wrong node.
    `find_node_types` returned both; `CallFunction|SelectOption` is the widget's.
    The same trap bit again on `Behavior|GetValue`, which has FIVE overloads and resolves to
    **Radial Slider**, not Spin Box — `Class|SpinBox|GetValue` is the one meant. Assume any
    unqualified id is ambiguous and check it; the `Class|<Type>|<Fn>` form is unambiguous.
  - `(== a b)` picks an overload by guess and fails on strings. Use the explicit node —
    `Utilities|String|IsEmpty`, `Utilities|String|EqualExactly(String)`.

  **So: read, then verify every node type with `find_node_types` and `get_node_type_pins`
  before writing, and read back afterwards.** A failed `write_graph_dsl` leaves the graph
  untouched, which is the one merciful part — both failures this session were recoverable.

- **Do NOT rewrite the EventGraph with `write_graph_dsl`.** It carries designer-bound widget
  events -- `(event OnClicked(SaveButton) ...)`, `(event OnSelectionChanged(AttachCombo) ...)` --
  and the writer cannot reproduce that binding. Rewriting it would silently unbind Save, Load
  and Bake. Related: UMG widgets are not `ActorComponent`s, so `add_component_bound_event`
  refuses them; there is no safe MCP route to a new designer-bound widget event, which is why
  the kit picker applies on Refresh rather than on selection.

- **Changing a Blueprint function's signature breaks its call sites until they are rewritten.**
  `add_function_param` on `FillCombo` made the body writable but left seven stale call nodes in
  `RefreshCombos`, and the next `write_graph_dsl` failed with "Could not find a pin for the
  parameter Slot". Rewrite the callers in the same sitting.

- **Check `git status` before every commit on this branch.** Two commits picked up baked
  Blueprints nobody staged on purpose — `f331996` (meant to add one test file) and `b59b9ef`
  (meant to add one markdown file), 4.5 MB of LFS each time. Both were baked while a commit was
  being prepared. The mechanism was NOT determined; `git config` shows nothing unusual and the
  `git add` pathspecs named only `Source/` and the one file. An audit of every Content-touching
  commit on the branch found only these two, both since untracked, and the three deliberate
  content commits are correct. Until the cause is known, verify what is staged rather than
  trusting the pathspec.

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
