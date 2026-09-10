# Procedural Dungeon Generator — Handoff to v2 (UE 5.8 + Unreal MCP)

**Written:** 2026-08-10
**From:** `E:\UE5 Projects\UE5_Procedural_Level_Generator` (UE 5.7, project name `Procedural_Dongeon`)
**To:** a fresh UE 5.8 project driven through the Unreal MCP server
**Source material:** Ronnie's 5-part "Ultimate Procedural Dungeon" YouTube series (full transcript in [transcript.txt](transcript.txt))
**Also reviewed:** `E:\Unity\Unity_Procedural_Level_Creator` — a second, more mature attempt at the same problem. See §9.

---

> ## STATUS — 2026-09-09
>
> **This document is a historical SOURCE, written before the port. Parts of it are now out of
> date, and it is kept as written rather than revised.** For the current state of the work,
> read [NEXT-SESSION.md](NEXT-SESSION.md) — that is the live handoff.
>
> **§9.2's room-authoring tool is ported and working**, on branch `port/room-authoring`.
> An authored room bakes straight into v1's `Master_Room` contract and the pieces drop into
> the existing Blueprint generator with no converter. 69 automation tests cover it.
>
> So wherever this document says the Unreal project has **"no equivalent"** of a
> room-authoring tool — §0, §6 and §9 all say it — read that as describing 2026-08-10, not
> today. The rest of §9's Unity comparison still stands: §9.1's generator work
> (`FPlacementFrame`, `FRoomExit`, the hall connector pool, the generation manifest) has
> **not** been ported, and Appendix A's "no equivalent" rows are still accurate for those.

---

## 0. How to use this document

Sections 1–3 describe **what exists and why**, so nothing gets lost in the port. Sections 4–6 are the **v2 design** — what to build differently and why. Section 7 answers open questions the tutorial left dangling. **Section 9 is the most actionable part of this document**: your Unity project already built and shipped working solutions to four of the problems listed in §3, plus a room-authoring tool the Unreal project has no equivalent of. Read §9 before writing any v2 code.

**§10 is the CCGS intake brief.** This document is a *source*, not a CCGS artifact — §10 says what to convert it into, in what order, and which decisions are already settled so the agent team doesn't re-litigate them. **If you are a CCGS agent reading this file, start at §10.**

The v1 logic lives entirely in Blueprint graphs inside `.uasset` binaries. It was reconstructed for this document by extracting the FName tables and import tables from the assets — so the **inventory of variables, events, and asset references is verified**, while the **exact node wiring is inferred** from those names plus the tutorial transcript. Where something is inferred rather than read directly, it says so. Open the v1 project alongside this doc when porting anything marked *(inferred)*.

---

## 1. What v1 actually is

A runtime, seed-deterministic dungeon assembler. It spawns a start room, then repeatedly picks a random unused exit, spawns a random room there, tests it for overlap, and keeps or destroys it. When the room budget is exhausted it caps leftover openings, then spawns doors, pickups, enemies and an exit.

### 1.1 Asset inventory — `Content/ProceduralLevel/`

| Asset | Role |
|---|---|
| `Room_Generator.uasset` | Original generator. Spawns from the grey-box `LevelPieces/` set. |
| `Dungeon_Room_Generator.uasset` | Second generator. Spawns from the art-dressed `LevelPieces/Dungeon/` (`1_*`) set. |
| `WBP_LevelGenerator.uasset` | UMG widget: seed input, level-name input, room-type combo box, Generate + Save buttons, status text. |
| `EUW_LevelBaker.uasset` | Editor Utility Widget wrapper around the above. **Not in the tutorial — your addition.** |
| `LevelPieces/Master_Room.uasset` | Base class every room piece inherits. Defines the room contract. |
| `LevelPieces/*.uasset` | Grey-box set: `Room1–4`, `Hall_1/2`, `Stairs_1/2`, `Start_Room`, `LargeRoom`, `CloseHole`. |
| `LevelPieces/Dungeon/1_*.uasset` | Art set: `1_Room1–4`, `1_Hall1/2`, `1_StarterRoom`, `1_CloseHole`. |
| `WorldInteractables/1_Door`, `Door`, `LevelExit` | Door actor and dungeon-exit trigger. |
| `Pickups/Pickup1` | The coin equivalent. |
| `Characters/Hero/BP_Hero`, `Characters/Enemies/BP_Enemy`, `AI_Destination` | Player, roaming enemy, and the debug arrow that marks the enemy's AI target. |
| `Levels/TestLevel1–4`, `TestLevel_1` | Baked output targets / test maps. |

Art comes from `Content/Fantastic_Dungeon_Pack/` (1331 files). `1_Room1` alone carries 120+ static mesh components — see §3.9.

### 1.2 The room contract — `Master_Room`

Every piece is a child of `Master_Room_C` and supplies four named scene-component "folders" plus a box:

| Component | Type | Purpose |
|---|---|---|
| `Exits Folder` | SceneComponent | Parent for Arrow components. Each arrow = one connection point, +X pointing **out** of the room. |
| `Overlap Folder` → `Overlap_Box` | SceneComponent → BoxComponent | Occupancy volume(s). Multiple boxes allowed for L-shaped rooms. |
| `FloorSpawnPoints` | SceneComponent | Parent for Arrow components marking valid floor spawn locations. |
| `GeometryFolder` | SceneComponent | All visual meshes. |
| `Arrow` | ArrowComponent | Orientation marker at room origin. |

**Two invariants the whole system depends on:**

1. **Room origin sits at the entrance**, not the room centre. A new room is spawned at the selected exit arrow's world transform, so its origin-at-entrance makes it line up automatically.
2. **Floor level is Z = 0** in every room, matching the exit arrows. The tutorial spends a painful stretch (transcript ~1216–1272) fixing rooms that violated this. Enforce it in v2 with a validation pass.

`Overlap_Box` uses a **custom object channel `RoomOverlap`** (`ECC_GameTraceChannel1`), declared in [Config/DefaultEngine.ini](Config/DefaultEngine.ini):

```ini
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel1,DefaultResponse=ECR_Ignore,bTraceType=False,bStaticObject=False,Name="RoomOverlap")
```

The box is set to object type `RoomOverlap`, ignores everything, and overlaps only `RoomOverlap`. **Port this ini block first** — without it every overlap test silently returns "clear" and rooms stack on top of each other.

### 1.3 Generator state (verified from the asset name tables)

```
ExitsList            Array<SceneComponent>  unused exit arrows, the frontier
SelectedExitPoint    SceneComponent         the exit currently being built onto
RoomList             Array<Class>           the list actually drawn from
BaseRoomList         Array<Class>           normal rooms
UniqueRoomList       Array<Class>           special rooms (stairs / boss / etc.)
LatestRoom           Master_Room            most recently spawned room
numberOfRooms        int                    remaining room budget
CurrentRoomIndex     int                    rooms placed so far
OverlapList          Array<PrimitiveComp>   overlap-test scratch buffer
DoorList             Array<SceneComponent>  exits that got a room — door candidates
FloorSpawnPoints     Array<SceneComponent>  every floor point across all placed rooms
FloorSpawnItemLocation SceneComponent       currently selected spawn point
PickupAmount         int                    pickup budget
Enemy Amount         int                    enemy budget
GeneratedActors      Array<Actor>           everything spawned — used for cleanup/baking
Seed / InitialSeed / NewSeed  int
SeedRandomStream     RandomStream           the deterministic source
BuildComplete        bool
MaxDongeonTime       float                  wall-clock failsafe threshold
TimerHandle          TimerHandle
LevelName            String                 bake target
```

### 1.4 Event flow *(inferred from event names + transcript)*

```
BeginPlay
 └─ SetSeed ─────────── Seed == -1 ? SeedRandomStream(random) : SetRandomStreamSeed(Seed)
 └─ Start Dungeon Timer  (1s looping timer → Check For Dongeon Complete)
 └─ SpawnStartRoom       spawn starter, append its exits to ExitsList
 └─ SpawnNextRoom ───────┐
      pick random exit from ExitsList via the stream
      pick random room from RoomList via the stream
      deferred-spawn at exit's world transform (AlwaysSpawn, ignore collision)
      cast to Master_Room → LatestRoom
      CheckForOverlap → AddOverlapingRoomsToList
        ├ OverlapList not empty → clear list, destroy LatestRoom, ──┘ retry
        └ OverlapList empty     → clear list
                                  remove SelectedExitPoint from ExitsList
                                  add SelectedExitPoint to DoorList
                                  append LatestRoom's exits to ExitsList
                                  AddFloorSpawnPoints
                                  numberOfRooms -= 1, CurrentRoomIndex += 1
                                  CurrentRoomIndex == 10 || == 20 ?  (inferred)
                                     RoomList = UniqueRoomList : RoomList = BaseRoomList
                                  numberOfRooms > 0 ? ──────────────┘ loop
                                                    : finish
 finish:
 └─ CloseOpenDoorway    spawn 1_CloseHole on every remaining ExitsList arrow
 └─ SpawnDoors          spawn 1_Door on DoorList arrows (stream-picked from a list
                        padded with None entries to thin them out)
 └─ SpawnPickupAtLocation / Spawn Enemies At Location / Spawn Exit
 └─ BuildComplete = true → ClearAndInvalidateTimerByHandle

Check For Dongeon Complete (every 1s):
   GetGameTimeInSeconds >= MaxDongeonTime → OpenLevel(LevelName)   // nuke and retry
```

---

## 2. Tutorial coverage — what you built, what you skipped

### Built ✅

| Feature | Tutorial part | Evidence in assets |
|---|---|---|
| Master room + exits/overlap/geometry folders | 1 | `Master_Room` components |
| Random exit + random room spawn loop | 1 | `SpawnNextRoom`, `ExitsList`, `RoomList` |
| Overlap rejection via `RoomOverlap` channel | 1 | `AddOverlapingRoomsToList`, ini channel |
| Room budget counter | 1 | `numberOfRooms` |
| Cap unused exits | 1 | `CloseOpenDoorway`, `1_CloseHole` |
| `BuildComplete` flag | 1 | `BuildComplete` |
| Wall-clock failsafe + `OpenLevel` restart | 1 | `MaxDongeonTime`, `TimerHandle`, `OpenLevel` |
| Seed system (`-1` = random, else fixed) | 2 | `SetSeed`, `SeedRandomStream`, `SetRandomStreamSeed` |
| Special room list swap at fixed indices | 2 | `UniqueRoomList`, `BaseRoomList`, `CurrentRoomIndex` *(inferred)* |
| Floor spawn point collection | 2 | `AddFloorSpawnPoints`, `FloorSpawnPoints` |
| Pickup spawning | 2 | `SpawnPickupAtLocation`, `Pickup1` |
| Door list + door spawning | 3 | `DoorList`, `SpawnDoors`, `1_Door` |
| Enemy spawning | 4 | `Spawn Enemies At Location`, `BP_Enemy` |
| Nav mesh (dynamic) + roaming AI | 4 | commits *"Navmesh creation"*, *"nav mesh bounds"*, `AI_Destination` |
| Exit zone | 5 | `LevelExit`, `Spawn Exit` |
| Multi-floor stair pieces | 2 | `Stairs_1`, `Stairs_2` — **grey-box set only** |

### Skipped or not carried into the art set ❌

| Feature | Tutorial part | Status |
|---|---|---|
| Treasure chest spawner | 2 | No chest asset. The pattern is identical to pickups — fold into the unified spawner (§4.4) rather than porting it as its own copy-pasted graph. |
| Boss room | 5 | No boss room asset. See §3.7 — the tutorial's own version places it at a *random* leftover exit, not at the end, so it's worth redesigning rather than reproducing. |
| Coin pickup → hero counter → gated exit | 5 | `BP_Hero` exists; no evidence of a `CoinAmount` counter or a gated `LevelExit`. |
| Door lock/unlock colour change + timeline | 3 | No door-light material logic found in `1_Door`. See §7.1 for why the tutorial's version only worked on one light. |
| Linear-dungeon mode | 5 | One-line change (clear `ExitsList` before appending the latest room's exits). Tutorial demonstrates it fails most of the time — see §3.2 for why, and fix that first. |
| Stairs / elevator in the `1_*` art set | 2, 4 | The Dungeon set is **single-floor**. Multi-floor exists only in the grey-box set. |

### Built beyond the tutorial 🌟

These are yours and are the most valuable things in the project. Preserve them.

- **`EUW_LevelBaker` + `WBP_LevelGenerator`** — generate in-editor and bake the result to a persistent `.umap`. Turns a runtime toy into an actual level-authoring tool.
- **Two generator classes selectable at runtime** via `CB_RoomType` → `SelectedRoomGeneratorClass`. A generic "ruleset" concept in embryo.
- **`GeneratedActors` tracking** — the list that makes clean regeneration and baking possible. The tutorial has no equivalent and leaks actors on every retry.
- **Real modular art** wired into the room contract, not cubes.

---

## 3. Design problems worth fixing in v2

Ranked by how much pain they cause. The first four are the ones that actually matter.

### 3.1 🔴 Placement is validated by physics, so generation can't be synchronous

This is the root cause of most other problems. The current test is: *spawn the actor → let components register → read `GetOverlappingComponents` → destroy if non-empty.* That requires the engine to tick between spawn and test, which is why the tutorial needs a `Delay` node at all, why the whole thing is timer-driven, and why it can't run cleanly in the editor without PIE.

**Fix:** decide the layout in **data** first. Keep an array of placed room AABBs (or a coarse occupancy grid, given rooms are on a grid). Test a candidate by transforming its box extents and doing AABB-vs-AABB against placed rooms. No spawn, no tick, no delay. Spawn actors only once the full layout is resolved.

Payoff: generation becomes a pure function `(seed, ruleset) → layout`, which is synchronous, instant, testable, runnable in the editor, and — critically for your MCP workflow — callable directly by an agent without driving PIE.

> **Already solved in Unity.** `V2LevelGenerator.Overlaps()` does exactly this with `Bounds.Intersects`. See §9.1 — including the rotation-aware bounds trap you'd otherwise hit.

### 3.2 🔴 Failure has no backtracking, so the failsafe fires constantly

On overlap the generator destroys the room and re-enters `SpawnNextRoom`, which picks a **fresh random exit and a fresh random room**. It never:

- tries a different room type at the same exit,
- marks an exit as exhausted after repeated failure,
- removes a dead exit from `ExitsList`.

So a blocked exit stays in the pool forever and can be re-picked indefinitely. That is exactly why the tutorial's linear-dungeon demo fails "most of the time" (transcript ~3507–3515) and why an 8–15 second wall-clock failsafe was needed.

**Fix:**
```
for each exit in shuffled(ExitsList):
    for each room in shuffled(RoomList):
        if fits(room, exit): place it; goto next iteration
    mark exit dead; remove from ExitsList
if ExitsList empty and budget remains: layout is stuck → restart with next seed
```
With a data-side fit test (§3.1) this whole search costs microseconds, so exhaustive retry is free.

> **Already solved in Unity.** A real placement stack with budget restore and a 50-backtrack cap. Port it nearly verbatim — see §9.1(b).

### 3.3 🔴 The failsafe is a wall-clock timer that reloads the level

Three separate problems:

1. **It compares absolute game time, not elapsed generation time.** `GetGameTimeInSeconds >= MaxDongeonTime` measures time since play began. If anything delays the start of generation, the budget is silently shorter. In the editor/baker path, where time doesn't reset per generation, this is worse.
2. **It's machine-dependent.** A slower PC produces different results from the same seed — which quietly breaks the determinism the seed system exists to provide.
3. **`OpenLevel` is a sledgehammer.** Reloading the whole map to retry a layout throws away everything, including the seed you were debugging.

**Fix:** budget by **iterations**, not seconds (`MaxPlacementAttempts`). On failure, destroy `GeneratedActors`, reset lists, increment the seed, and re-run **in place**. Keep `OpenLevel` out of the generator entirely.

### 3.4 🟠 Five copy-pasted spawn graphs

`SpawnPickupAtLocation`, `Spawn Enemies At Location`, `Spawn Exit`, and (per the tutorial) chest and boss are near-identical graphs differing only in class, count, and Z-offset. Ronnie flags this himself at transcript ~1725–1741 and ~3941–3949.

**Fix:** one function —

```cpp
SpawnAtFloorPoints(TSubclassOf<AActor> Class, int32 Count, float ZOffset, bool bConsumePoint)
```

driven by a `DataTable` / `UDataAsset` of spawn rules. Adding a chest becomes a data row, not a graph.

While you're there: the hardcoded `+60` Z-offset for enemies (transcript ~3069–3080) is a workaround for `BP_Enemy`'s capsule being centred at origin. Put the offset in the spawn rule, or fix the pivot.

### 3.5 🟠 Special-room selection is fragile

`RoomList` gets **overwritten** from `BaseRoomList` or `UniqueRoomList` on every iteration based on `CurrentRoomIndex == 10 || == 20`. Three variables tracking one concept, with the "current" one mutated in place, and hardcoded index literals. Nothing removes a special room from `UniqueRoomList` once used, so the same one can repeat — Ronnie names this as unsolved (transcript ~1380–1384).

**Fix:** one weighted room table with per-entry rules:

```
Room1     weight 10  min 0  max ∞
Hall1     weight  6  min 0  max ∞
Stairs    weight  0  min 2  max 2   placeAtDepth: [10, 20]
BossRoom  weight  0  min 1  max 1   placeAt: deepest
```
Selection reads the table; `max` decrements as rooms are placed. No list swapping, no mutation.

> **Already solved in Unity.** `CategoryPool` — weighted draw from remaining counts, with `Increment()` to restore the budget when a placement is rolled back. §9.1(b).

### 3.6 🟠 Door frequency is controlled by padding an array with `None`

To get ~50% doors, the tutorial adds a `None` entry to `DoorActorList` (transcript ~2056–2067). Functional but opaque — the ratio is implicit in array length, and it burns a spawn attempt on nothing.

**Fix:** `DoorSpawnChance` float, rolled against the stream. Keep the door **type** list separate from the door **frequency**.

### 3.7 🟠 No progression structure

Rooms attach only to unused exits, so the layout is always a **tree** — connected, but with no loops, no critical path, and no notion of depth. Consequences:

- The boss room lands on a random leftover exit, which can be adjacent to the start.
- Keys and locked doors are unimplementable safely: a key can spawn behind the door it opens (Ronnie raises exactly this at transcript ~2069–2079).
- The exit zone spawns at a random floor point, possibly next to the player start.

**Fix:** the layout pass already knows the parent of every room, so **record depth** (start = 0). Then:

- boss / exit → room with max depth
- keys → any room with `depth < lockedDoor.depth`
- optionally, post-pass: connect two rooms whose boxes are adjacent but unlinked, to create loops

This is cheap once §3.1 is in place and it's the single biggest gameplay upgrade available.

> **Already solved in Unity.** The V2 generator is explicitly **spine + branches**: a guaranteed critical path Starter → … → Boss, with side rooms attached afterwards. That's the structure this section is asking for, already designed and working. §9.1(a).

### 3.8 🟡 Nav mesh generates on top of walls

Enemies pick unreachable targets on wall tops and stall (transcript ~3246–3253). Ronnie couldn't solve it.

**Fix (any of):**
- Set wall static meshes to **`Can Ever Affect Navigation` = false** — cleanest for a modular kit where walls are always walls.
- Or add a `NavModifierVolume` with area class `NavArea_Null` over wall tops.
- Or raise `AgentMaxStepHeight` / `CellHeight` on `RecastNavMesh-Default` so thin ledges aren't rasterised.

Also: the tutorial's giant dynamic nav volume is a perf trap, and Ronnie notes AI stops working far from centre (~3134–3139). **Use nav invokers** (`bGenerateNavigationOnlyAroundNavigationInvokers = true`, `NavigationInvoker` component on the player) instead of a huge always-on dynamic volume.

### 3.9 🟡 Room pieces are hundreds of loose static mesh components

`1_Room1` has 120+ `StaticMesh` components. That's 120 component registrations, transform updates and draw calls per room, times ~30 rooms.

**Fix:** merge each room's static geometry to a single mesh (Actor Merging / `UStaticMesh` bake), or use `InstancedStaticMeshComponent` / `HierarchicalISM` per mesh type. UE 5.8's ISM tooling makes this straightforward, and there's an `ism_ops` MCP tool available. Expect a large frame-time win.

### 3.10 🟡 Hand-placed floor spawn points don't scale

Every room needs arrows manually placed for every possible spawn location. Ronnie names this as unsolved twice (~1650–1655, ~3609–3617) and floats the fix himself: a volume + downward traces.

**Fix options, in ascending order of power:**
1. A `FloorSpawnVolume` box per room; sample points via the stream and line-trace down to the floor, rejecting hits on non-floor surfaces.
2. **PCG** (built into 5.7/5.8, and an `pcg_ops` MCP tool exists). Surface sampler on the room floor with density/exclusion rules — this is exactly what PCG is for, and it removes hand-placement entirely.
3. **Derive them from the room's cell grid**, if you adopt the Unity room builder (§9.2) — every filled floor cell is a candidate spawn point, for free, with no authoring at all.

### 3.11 🟡 Project hygiene

- **`Dongeon` typo is baked into the module name.** `Procedural_Dongeon`, `MaxDongeonTime`, `Check For Dongeon Complete`. Renaming a UE module after the fact is genuinely painful — fix it now, at project creation, not later.
- **`Source/` is an empty stub.** `MyObject.h` is a bare `UObject` with no body; `Build.cs` pulls only Core/CoreUObject/Engine/InputCore. The module is declared in the `.uproject` but does nothing. Either give it real content (§4.1) or don't create it.
- **`Intermediate/` and `Saved/` are tracked in git** despite being in [.gitignore](.gitignore) — they were committed before the ignore rules landed, so they dirty every diff. In v2, get `.gitignore` and `.gitattributes` (LFS) right in the very first commit.
- **`Config/DefaultGame.ini` still says `ProjectName=Third Person BP Game Template`.**

---

## 4. Recommended v2 architecture

### 4.1 Split: C++ decides, Blueprint dresses

| Layer | Language | Contents |
|---|---|---|
| Layout solver | C++ | `FDungeonLayout Solve(const FDungeonRuleset&, int32 Seed)` — pure, no world access, no actors. Returns an array of `{RoomClass, Transform, Depth, ParentIndex, OpenExits[]}`. |
| Spawner | C++ | Consumes a layout, spawns actors, tracks them for teardown. |
| Ruleset | Data | `UDungeonRulesetAsset` — room table with weights/min/max/depth rules, spawn rules, door chance, budgets. |
| Room pieces | Blueprint | `Master_Room` children. Art and component layout only, zero logic. |
| Editor tool | Blueprint (EUW) | Your existing baker, re-pointed at the C++ entry points. |

Why this matters for your workflow specifically: a pure C++ solver is **directly callable from MCP**, unit-testable via UE's automation framework (there are `run_tests` / `list_tests` MCP tools), and doesn't need PIE. Blueprint-only generation forces every experiment through a play session.

### 4.2 Room metadata belongs on the room, not in the generator

Add to `Master_Room` (or better, a `URoomDefinition` data asset per room):

```
FBox     LocalBounds        // computed from Overlap_Box children at cook/construction time
TArray<FTransform> Exits    // baked from Exit arrows, so the solver never touches components
ERoomTag Tags               // Start, Normal, Stairs, Boss, DeadEnd
int32    MinDepth/MaxDepth
```

Bake these in the construction script. The solver then reads plain structs and never needs a spawned actor — which is what makes §3.1 possible.

### 4.3 Determinism contract

State it explicitly and test it: **same seed + same ruleset → byte-identical layout, on any machine, at any frame rate.**

Every random draw goes through `SeedRandomStream`. That already holds in v1 — keep it. The one thing that breaks it in v1 is the wall-clock failsafe (§3.3); removing it restores the guarantee. Add an automation test that solves the same seed 100× and asserts identical output.

### 4.4 Unified spawn rules

```
struct FDungeonSpawnRule {
    TSubclassOf<AActor> Class;
    int32   Count;              // or MinCount/MaxCount rolled from the stream
    float   ZOffset;
    bool    bConsumePoint;      // remove the point so nothing else spawns there
    ERoomTag AllowedRooms;      // e.g. never spawn enemies in the start room
    int32   MinDepth;           // e.g. keys only in shallow rooms
};
```

One loop consumes the whole array. Pickups, chests, enemies, exit, keys and boss all become rows.

### 4.5 MCP-specific notes

- `status` reports `editor_online: false` when UE isn't running — check it before any asset op.
- The current [.mcp.json](.mcp.json) points at this project. Re-point `MCP_UNREAL_PROJECT` at the new 5.8 `.uproject`.
- Useful tools for this project: `pcg_ops` (§3.10), `ism_ops` (§3.9), `blueprint_modify` / `blueprint_query`, `run_tests`, `level_ops`, `spawn_actor`, `execute_script`.
- **Keep the layout solver reachable from `execute_script`/`call_function`** so an agent can generate 100 layouts and inspect statistics (room count distribution, failure rate, depth histogram) without ever launching PIE. This is the single highest-leverage design decision for agent-driven iteration.

---

## 5. Port checklist

**Foundation**
- [ ] Create the 5.8 project with a correctly-spelled name (`ProceduralDungeon`), C++ template.
- [ ] Commit `.gitignore` + `.gitattributes` (copy LFS rules from v1) **before** the first content commit.
- [ ] Port the `RoomOverlap` object channel into `DefaultEngine.ini` — even if the solver goes data-side, gameplay collision still wants it.
- [ ] Set `ProjectName` in `DefaultGame.ini`.

**Content migration**
- [ ] Migrate `Content/Fantastic_Dungeon_Pack/` (5.7 → 5.8 asset upgrade; verify materials and Nanite settings survive).
- [ ] Migrate `Content/ProceduralLevel/LevelPieces/Dungeon/` (the `1_*` set) and `Master_Room`.
- [ ] Migrate `WorldInteractables/`, `Pickups/`, `Characters/`.
- [ ] **Do not migrate** `Room_Generator` / `Dungeon_Room_Generator` — rebuild them per §4. Keep v1 open for reference.
- [ ] Consider leaving the grey-box `LevelPieces/` set behind, or keep it as a fast test ruleset (it's genuinely useful for iterating on the solver without art load times).

**Rebuild** *(most of this is a port from the Unity project, not a fresh design — see §9.5)*
- [ ] C++ layout solver with AABB fit test + exhaustive retry (§3.1, §3.2 — port `V2LevelGenerator`).
- [ ] Rotation-aware bounds, with the offset rotated (§9.1(d)) — silent bug otherwise.
- [ ] Cardinal Y re-snap after every placement (§9.1(f)) — prevents drift across long chains.
- [ ] Exit state as `isConnected` / `isSealed` flags, replacing `ExitsList` + `DoorList` (§9.1(c)).
- [ ] Spine + branches topology with depth tracking (§3.7, §9.1(a)).
- [ ] Halls as a distinct connector pool; reclassify `1_Hall1` / `1_Hall2` (§9.1(e)).
- [ ] Manifest output + fail-fast pool validation (§9.1 g–h) — do this early, everything after is easier to debug.
- [ ] Iteration-budget failsafe, in-place regeneration, no `OpenLevel` (§3.3).
- [ ] `URoomDefinition` / room metadata baking (§4.2).
- [ ] Weighted room table replacing the three-list swap (§3.5).
- [ ] Unified `FDungeonSpawnRule` spawner (§4.4).
- [ ] Explicit `DoorSpawnChance` (§3.6).
- [ ] Determinism automation test (§4.3).

**Content fixes**
- [ ] Validation pass: every room's floor at Z=0, exits at Z=0, overlap boxes covering the footprint.
- [ ] Walls → `Can Ever Affect Navigation = false` (§3.8).
- [ ] Nav invokers instead of a giant dynamic nav volume (§3.8).
- [ ] Merge or instance room geometry (§3.9).
- [ ] Stairs/elevator pieces for the `1_*` art set — the art set is currently single-floor.

**Restore your extras**
- [ ] Rebuild `EUW_LevelBaker` / `WBP_LevelGenerator` against the new C++ entry points. Now that generation is synchronous, the widget can generate and preview instantly with no PIE round-trip.
- [ ] Keep `GeneratedActors` teardown — it's what makes regenerate-in-place work.
- [ ] Ruleset picker in the widget (replaces the two-generator combo box, generalised).

**Then, features skipped in v1**
- [ ] Chest (a data row, once §4.4 exists).
- [ ] Boss room at max depth (§3.7).
- [ ] Coin counter on `BP_Hero` + gated `LevelExit`.
- [ ] Door lock/unlock visuals (§7.1 has the fix for the tutorial's bug).
- [ ] Key/lock progression using depth ordering.

---

## 6. Ordering advice

Do **§3.1 (data-side fit test) first**. It's the keystone: §3.2, §3.3, §3.7, editor-time generation, determinism and MCP-callability all become easy once layout is a pure function, and all stay hard while it isn't. Everything else in this document is downstream of that one change.

Second, do §4.2 (room metadata baking) — the solver needs it.

Everything after that is independent and can be done in any order.

**But read §9 first.** The Unity project is a working reference implementation of §3.1, §3.2, §3.5 and §3.7 together. Porting `V2LevelGenerator.cs` to C++ is a smaller job than designing those four fixes from scratch, and it comes with a room-authoring tool that removes most of the manual geometry work.

---

## 7. Open questions the tutorial left unanswered

### 7.1 Why the Timeline in a custom event only animated one light

Ronnie hits this at transcript ~2563–2612 and ~2705–2708: he made a `door lights` custom event taking a target component, called it 4× with a `Sequence`, and only one light changed colour. He had to duplicate the graph four times with four separate Timelines.

**The reason:** a Blueprint Timeline is a **component instance on the Blueprint**, not a local variable. There is exactly one of it per actor, with one playhead and one set of output bindings. Calling the custom event four times doesn't create four timelines — it restarts the *same* timeline three times and rebinds its update target, so only the last caller's target receives updates. This is why the Sequence + four duplicated Timelines worked: four distinct component instances.

**Correct fixes, best first:**
1. Put the animation **inside the light actor/component itself**, so each instance owns its own Timeline. Call `SetUnlocked()` on each; each animates independently.
2. Use a **Material Parameter Collection** or a shared dynamic material instance — set one scalar, all four lights read it. Cheapest by far.
3. Drive the lerp from a `SetTimerByEvent` or `Tick` with an explicit per-target alpha stored in a map.

### 7.2 What a Random Stream actually is

Ronnie repeatedly says he doesn't understand it (~1019–1032, ~1066–1073). It's a **pseudo-random number generator with explicit, serialisable state**. The seed is its starting state; every draw advances that state deterministically. Same seed + same sequence of draws = same numbers. That's the whole thing.

The practical consequence, which the tutorial never states: **the order and count of draws is part of the seed contract.** Adding one extra `RandomFromStream` call anywhere earlier in generation shifts every subsequent draw, so the same seed produces a different dungeon. Any change to the generation algorithm invalidates saved seeds. Worth documenting for players if you ever ship seed sharing.

### 7.3 Printing the seed

Answered in the transcript itself (~2659–2680, from viewer comments): drag from the stream → `Break RandomStream` → gives the integer seed. Already reflected in `SeedRandomStream` usage in v1.

### 7.4 `SetRelativeLocation` vs `SetWorldLocation` on the door

Ronnie's confusion at ~2149–2255. **Relative** location is offset from the parent component; **world** is absolute. The door mesh is a child of the door actor's root, and the actor is spawned at arbitrary positions/rotations across the dungeon — so a world-space Z offset would be wrong for any rotated door, and would fight the actor transform. Relative is correct here. The "numbers changed after adding the lerp" mystery is just that the lerp changed what the endpoints meant (offset-from-current vs absolute-relative), not an engine quirk.

---

## 9. Cross-project: what to lift from the Unity build

**Source:** `E:\Unity\Unity_Procedural_Level_Creator`

That project contains a second attempt at the same problem, taken considerably further. Its V2 generator ([`V2LevelGenerator.cs`](../../Unity/Unity_Procedural_Level_Creator/Assets/Scripts/LevelGen/V2/Editor/V2LevelGenerator.cs), 1075 lines) independently arrived at four of the fixes recommended in §3 — **and has them working**, with a design spec ([`V2_LevelGenerator_DesignSpec.md`](../../Unity/Unity_Procedural_Level_Creator/V2_LevelGenerator_DesignSpec.md)) recording the reasoning and the resolved edge cases.

It also contains a **room-authoring tool** the Unreal project has no equivalent of, which is arguably worth more than the generator.

### 9.0 Concept map

| Unity | Unreal v1 | Verdict |
|---|---|---|
| `RoomPiece` — bounds as data, exit list, `generationDepth`, `categoryName` | `Master_Room` — components only | Unity's is a proper data contract. Adopt. |
| `ExitPoint` — direction enum + `isConnected` / `isSealed` / `connectedPiece` | Arrow under `Exits Folder` | Unity's is a stateful socket. Adopt. |
| `Bounds.Intersects` AABB test | `GetOverlappingComponents` physics query | Confirms §3.1. Adopt. |
| `Frame` stack + `MaxBacktracks = 50` | destroy-and-repick | Confirms §3.2. Adopt. |
| `CategoryPool` weighted draw + restore | 3-list swap on room index | Confirms §3.5. Adopt. |
| Spine → Boss, then branches | flat exit frontier | Confirms §3.7. Adopt. |
| Halls as structural connector pieces | rooms butt directly together | New idea. Adopt — §9.1(e). |
| Manifest text file | nothing | New idea. Adopt — §9.1(g). |
| `CellMap` / `EdgeSolver` / `RoomBuilder` | nothing | New system. The biggest win — §9.2. |

### 9.1 The V2 generator — port this

**(a) Spine-then-branches topology.** This is §3.7, already designed:

```
spineLength = (small + medium + large + special) − branchSlotCount
place Starter at origin
for i in 0..spineLength-1:  place a room from the pool on the previous room's exit
place Boss at the end of the spine
for each branch slot:  pick any placed room with a free exit, attach a room there
```

Every room therefore has a well-defined position on or off a **critical path from Starter to Boss**. That's the structure that makes keys, locked doors, difficulty ramping and a meaningful boss placement possible — and the Unreal version has never had it. Port as `SolveSpine()` then `SolveBranches()`.

**(b) Backtracking with budget restore.** The `Frame` records `(priorRoom, priorExit, hall, room, category, slotIndex)`. On a dead end:

```
pop the frame
pool.Increment(frame.category)              // give the budget back
triedAtSlot[frame.slotIndex].Add(category)  // don't re-try that category here
destroy the room + hall actors
frame.priorExit.isConnected = false         // reopen the exit
backtracks++; if (backtracks > 50) fail with a diagnostic
```

That is §3.2 and §3.5 solved together, in about 25 lines. Port nearly verbatim.

**(c) Exit state lives on the socket, not in a list.** `isConnected` / `isSealed` / `connectedPiece` on the `ExitPoint` itself.

Unreal v1 tracks the frontier by adding to and removing from `ExitsList`, which is why it needs a *second* parallel array (`DoorList`) to remember which exits got used, and why backtracking is impossible (removal is destructive). Flags make "which exits on this room are free?" a local query, make `DoorList` unnecessary (a door goes wherever `isConnected == true`), and make rollback a single assignment.

**(d) Rotation-aware bounds — a trap you would otherwise hit.**

```csharp
static Bounds GetRotationAwareWorldBounds(RoomPiece piece) {
    Bounds raw = piece.GetWorldBounds();
    bool quarterTurn = yEuler == 90f || yEuler == 270f;
    if (!quarterTurn) return raw;
    return new Bounds(raw.center, new Vector3(sz.z, sz.y, sz.x));  // swap X/Z
}
```

Half-extents authored in local space **do not rotate with the actor**. Rooms are square in the Unreal grey-box set, so this bug stays invisible until the first non-square room, then produces phantom overlaps that look random. Because rotations are constrained to cardinal Y, the swap is exact — no conservative OBB needed.

Two things to fix while porting, rather than copy:
- Unity's `GetWorldBounds()` computes `transform.position + boundsOffset` **without rotating the offset**. Latent bug for any room with a non-zero offset at 90°/270°. Rotate it.
- In UE, `FBox::TransformBy(FTransform)` handles both correctly — prefer it over hand-rolled extent swapping if you keep bounds as an `FBox`.

**(e) Halls as first-class connector pieces.** Connections are a 3-piece chain:

```
Room1.exit → Hall.entry ,  Hall.exit → Room2.entry
```

with **separate size choices for spine halls and branch halls**. Unreal v1 butts rooms directly, so inter-room spacing is baked into room geometry and every room must share one module pitch. Halls decouple that, and "long spine halls, short branch halls" is a one-dropdown change that dramatically alters how a dungeon reads.

Note this reclassifies your existing pieces: `1_Hall1` / `1_Hall2` currently sit in the **room** list. In the Unity model they'd be a separate hall pool with entry/exit sockets.

**(f) Cardinal re-snap after every placement.** `SnapExitToExit` builds a delta quaternion so the incoming socket's forward is the negation of the target's, then:

```csharp
static void SnapToCardinalY(Transform t) {
    float ySnapped = Mathf.Round(t.eulerAngles.y / 90f) * 90f;
    t.rotation = Quaternion.Euler(0f, ySnapped, 0f);
}
```

That re-snap is what stops floating-point rotation error accumulating across 30 chained rooms. Unreal v1 chains `MakeTransform` off each arrow with no correction, so drift compounds silently. Pair it with `FindExitWithWorldForward`'s `Dot > 0.99` direction match.

**(g) The manifest.** On save, a `{sceneName}_manifest.txt` sibling records: seed, every input parameter, an ordered placement table (`# / kind / category / prefab / world position / Y rotation`), backtrack count, and elapsed time.

This is the cheapest debugging tool in either project, and Unreal v1 has nothing like it. For agent-driven work it beats a screenshot: two manifests can be diffed to see exactly what a parameter change did, with no PIE session and no eyeballing.

**(h) Fail-fast validation with actionable messages.** Before touching the scene, every pool the run will need is checked, and failures name the folder and the setting that required it:

> `smallCount=3 but Assets/Prefabs/Level Prefabs/Rooms/Small/ has no RoomPiece prefabs.`

Unreal v1 silently generates a broken dungeon instead.

**(i) Editor-generate + explicit save.** You already have this shape in `EUW_LevelBaker`; Unity's is more polished — destroys any prior root before re-generating, registers undo, prompts on overwrite, frames the camera to the level bounds, writes the manifest alongside, closes the temp scene afterwards. Port the polish.

**Known gap — fix it during the port.** `TryPlaceSpineSlot` picks **one** random exit on the prior room and never tries the others; it only varies category, prefab, rotation and hall. The design spec says it should also try a different exit (spec §"Spine", step 3), but the code doesn't. Backtracking papers over it at the cost of extra backtracks and occasional spurious failures. Add the exit loop.

### 9.2 The room-authoring tool — the bigger win

`CellMap` + `TileType` + `EdgeSolver` + `RoomBuilder`. Nothing like this exists on the Unreal side, and it attacks the single largest time sink in the whole tutorial.

**What it does:** you paint a floor plan on a grid; the solver emits floors, walls and corners automatically.

- `Cell` = `{ TileType, tier, rotSteps }` — three bytes. `CellMap` is a flat array with `CellSize = 4`, `TierHeight = 6`, max 3 tiers.
- **`HasWallOnEdge(x, z, edge)` is the whole idea.** A wall exists where a solid cell meets empty, or steps *down* to a lower tier; when neighbours differ in height the higher cell owns the shared wall; a doorway mark suppresses the wall entirely. That one predicate replaces every manually-placed wall in the project.
- `TileType` covers Square, four Triangles, four Quarter-circles, Angle / Concave / Convex / Circle, each with an edge-occupancy bitmask and `OccupiesEdgeRotated()` doing the rotation math. **Non-rectangular rooms become authored data rather than hand-assembled geometry** — the exact thing Ronnie spends ~15 minutes of part 4 failing to do by hand for one corner room.
- `tier` gives **multi-level rooms inside a single piece** — the elevator/stair room, correct by construction.
- **`PopulateRoomPiece(map)` derives the bounds *and* the exit sockets automatically:** bounds from `width × CellSize` and `(maxTier+1) × TierHeight`; one `ExitPoint` per doorway mark, positioned at the cell-edge midpoint with `LookRotation(outwardDir)`. **No hand-placed arrows. No hand-tuned overlap box. Ever.** That closes both invariants from §1.2 — the two things the tutorial breaks repeatedly and spends the most time repairing.
- Wall **colliders are emitted as contiguous runs per edge**, not one per wall segment — directly relevant to §3.9.
- `ToAscii()` dumps a floor plan as text. Unit-testable, and pasteable straight into an agent conversation.

**Why this matters more than the generator port:** the generator's output quality is capped by the room library. Your `1_*` rooms are 120-component hand assemblies, and the tutorial spends more screen time wrestling geometry into alignment than writing generation logic. A grid-based builder turns a new room into minutes of work instead of hours — and makes it *correct by construction*.

**Unreal mapping:**

| Unity | UE 5.8 |
|---|---|
| `CellMap` grid data | `USTRUCT` + a `UDataAsset` per room |
| `EdgeSolver` → `FloorPlacement` / `WallPlacement` lists | same — pure structs, no actors |
| `RoomBuilder.Build()` instantiating prefabs | Editor Utility + **ISM/HISM per mesh type** (§3.9 solved for free) |
| `PopulateRoomPiece()` | construction script deriving `LocalBounds` + baked exit transforms (§4.2) |
| `PieceCatalogue` (TileType + WallKind → prefab) | `UDataAsset` per art set |
| `RoomBuilderEditor` window | Editor Utility Widget |

The catalogue indirection is what makes rooms **theme-able**: one CellMap, different catalogue → the same room rendered in Fantastic Dungeon Pack or in grey-box. That's `CB_RoomType` generalised properly, and it means you can iterate on layout with fast-loading grey-box and swap to final art with one field.

### 9.3 Do NOT port

- **`LayoutStyle` Grid / Organic / Corridor** — enum values exist, never implemented.
- **`branchingFactor`, `deadEndCount`, `secretRoomCount`** — surfaced in the UI, logged to the manifest, and completely ignored by the generator. Don't build the control until the behaviour exists; a knob that does nothing is worse than no knob.
- **Theme-aware selection** — deferred in Unity too. The manifest logs the theme name and then pulls from raw folders anyway. Build the catalogue indirection properly instead (§9.2).
- **The unrotated `boundsOffset`** (§9.1(d)) — a latent bug, not a pattern.

### 9.4 Unit and axis conversion

The Unity project is in **metres**; Unreal is in **centimetres**. `Documentation/UE5_Port_Plan.md` already establishes ×100 for the player port — reuse that convention:

| Unity | Unreal |
|---|---|
| `CellSize = 4` | **400 uu** |
| `TierHeight = 6` | **600 uu** |

⚠️ **Verify both against the Fantastic Dungeon Pack module pitch before committing.** The cell size must equal the art kit's floor-tile size or nothing aligns — and that number is far more expensive to change after rooms are authored than before.

Axes: Unity is Y-up, Unreal is Z-up; both left-handed. Unity's `Direction.North → Vector3.forward (+Z)` maps to Unreal **+X**, which is already the convention the tutorial's exit arrows use. Grid `(x, z)` → Unreal `(X, Y)`. `TierHeight` moves from Unity `+Y` to Unreal `+Z`.

### 9.5 Revised plan

The recommendation in §4 stands, but §9 changes how you get there — from *design it* to *port it*:

1. **Port `RoomPiece` / `ExitPoint` as UE data types** (§9.1(c), §4.2). Small, and everything depends on it.
2. **Port `V2LevelGenerator` to C++** — spine + branches + backtracking + AABB fit, in one pass (§9.1 a–d, f). This delivers §3.1, §3.2, §3.5 and §3.7 together.
3. **Add halls as a distinct pool** (§9.1(e)) and reclassify `1_Hall1` / `1_Hall2`.
4. **Add the manifest and fail-fast validation** (§9.1 g–h) before anything else, so every later step is debuggable.
5. **Rebuild `EUW_LevelBaker`** against the C++ entry points, with the Unity save-flow polish (§9.1(i)).
6. **Then** the room builder (§9.2) — the largest piece, and independent of everything above. It can proceed in parallel with 2–5 if you want, since it only produces room assets the generator consumes.

---

## 10. CCGS intake brief

**Read this section first if you are a CCGS agent.**

This file is a **source document**, not a CCGS artifact. CCGS's skills read from fixed paths (`design/gdd/`, `docs/architecture/adr-*.md`, `production/epics/`). Nothing in this file is in those paths, so no skill will find it automatically. §10 says what to convert it into.

Most of the design work is already done here. The job is **transcription into CCGS's formats**, not rediscovery.

### 10.0 ⚠️ Blocker: fix the engine pin before anything else

The CCGS template's Unreal reference (`docs/engine-reference/unreal/VERSION.md`) is pinned to **UE 5.7**, last verified 2026-02-13. You are targeting **5.8**.

Every engine-specialist agent cross-references that directory before proposing an API. Left at 5.7, they will confidently generate 5.7-era code. Run this **before any other CCGS command**:

```
/setup-engine upgrade 5.7 5.8
```

Also note the template's root `CLAUDE.md` hard-codes `@docs/engine-reference/godot/VERSION.md`. Confirm `/setup-engine` repoints that at `unreal/` — if it doesn't, fix it by hand.

Useful reference material already present: `docs/engine-reference/unreal/plugins/pcg.md` (relevant to §3.10) and `modules/navigation.md` (relevant to §3.8).

### 10.1 Command order

```
1.  /setup-engine upgrade 5.7 5.8      ← blocker, see §10.0
2.  /adopt full                         ← only if reusing an existing CCGS project;
                                          skip for a fresh clone
3.  /design-system  "Procedural dungeon generator"
                                        ← GDD, sourced from §1, §4, §9 of this file
4.  /create-architecture                ← ADRs; feed it §10.2 so it doesn't re-derive
5.  /create-epics  →  /create-stories   ← use the epic breakdown in §10.4
6.  /dev-story  (per story)
7.  /gate-check                         ← at each epic boundary
```

Skip `/brainstorm` and `/start`. The concept is settled and the prior art is documented; guided ideation would only re-open closed questions.

### 10.2 Pre-decided ADRs — do not re-litigate

Twelve architectural decisions are already made and justified in this document. Write them as `docs/architecture/adr-NNN-*.md` before running `/create-architecture`, so its ADR audit sees them as existing decisions rather than gaps.

Each has real evidence behind it: either a named failure mode in the v1 Unreal build, or a working implementation in the Unity build.

| # | Decision | Rationale lives in | Evidence |
|---|---|---|---|
| 1 | Layout is solved in **data** (AABB vs. placed-room list), never by spawning and reading physics overlaps | §3.1, §9.1 | v1 needs a tick delay and can't run outside PIE; Unity's `Bounds.Intersects` version works |
| 2 | Generation is a **pure synchronous C++ function** `(seed, ruleset) → layout`; Blueprint holds art and content lists only | §4.1 | Enables editor-time generation, automation tests, and direct MCP calls |
| 3 | Failure budget is **iteration count**, not wall-clock; on failure **regenerate in place**, never `OpenLevel` | §3.3 | v1's wall-clock failsafe is machine-dependent and silently breaks seed determinism |
| 4 | Topology is **spine + branches** with per-room depth recorded | §3.7, §9.1(a) | v1's flat frontier makes keys/locks/boss placement impossible; Unity's spine model works |
| 5 | Exit state is **flags on the socket** (`bIsConnected` / `bIsSealed`), not list membership | §9.1(c) | Removes the parallel `DoorList`, and makes backtracking a single assignment |
| 6 | **Exhaustive backtracking** with a placement stack and budget restore, capped at N | §3.2, §9.1(b) | v1 re-picks at random and never retries a blocked exit — the root cause of its restart loops |
| 7 | **Halls are a distinct connector pool**, not entries in the room list | §9.1(e) | Decouples room spacing from room geometry; `1_Hall1`/`1_Hall2` are currently misclassified |
| 8 | Room content is **data-driven** (weighted room table + `FDungeonSpawnRule` array) | §3.5, §4.4 | Replaces the 3-list swap and five copy-pasted spawn graphs |
| 9 | Rooms are **authored on a cell grid**; bounds and exit sockets are **derived**, never hand-placed | §9.2 | Hand-placement is the largest source of authoring error and time cost in v1 |
| 10 | **Determinism contract:** same seed + ruleset ⇒ identical layout on any machine at any frame rate, enforced by an automation test | §4.3 | Currently violated by ADR-3's predecessor |
| 11 | Room geometry is **instanced or merged**, never loose per-mesh components | §3.9 | `1_Room1` carries 120+ static mesh components, × ~30 rooms |
| 12 | Navigation uses **invokers**; wall meshes are excluded from navigation generation | §3.8 | Fixes the unreachable wall-top AI targets the tutorial never solved |

Two of these deserve explicit sign-off from the technical director before work starts, because they are expensive to reverse: **ADR-2** (C++ vs. Blueprint split) and **ADR-9** (cell-grid authoring). Everything else is contained.

### 10.3 GDD / systems map

`/design-system` should produce roughly this set. Sources are all in this file:

| GDD | Source sections |
|---|---|
| `dungeon-generation.md` — the solver: topology, budgets, backtracking, determinism | §1.4, §3.1–3.3, §3.7, §4.1–4.3, §9.1 |
| `room-authoring.md` — cell grid, tile types, tiers, edge solving, catalogues | §1.2, §9.2 |
| `dungeon-content.md` — spawn rules, doors, pickups, enemies, exit, keys/locks | §3.4–3.6, §4.4 |
| `level-baking-tools.md` — editor widget, manifest, save flow | §2 (🌟 row), §9.1(g)–(i) |

`design/gdd/systems-index.md` should list all four and note that this file is their shared source.

### 10.4 Epic breakdown

Ordered by dependency. E1–E5 are the critical path; E6 is the largest single piece but independent of E3–E5 and can run in parallel.

| Epic | Scope | Depends on |
|---|---|---|
| **E1 — Project foundation** | 5.8 project, correct module name, gitignore/LFS first commit, `RoomOverlap` channel, config hygiene | — |
| **E2 — Room data contract** | `URoomDefinition`, `FRoomExit`, baked bounds + exit transforms, validation pass | E1 |
| **E3 — Layout solver** | Port `V2LevelGenerator` to C++: spine + branches, backtracking, AABB fit, rotation-aware bounds, cardinal re-snap | E2 |
| **E4 — Observability** | Manifest output, fail-fast pool validation, determinism automation test | E3 (start early — everything after is easier to debug) |
| **E5 — Content spawning** | Unified `FDungeonSpawnRule`, weighted room table, door chance, depth-gated boss/exit/keys | E3 |
| **E6 — Room builder** | Cell grid, tile types, edge solver, catalogue indirection, editor widget, ISM output | E2 only |
| **E7 — Content migration** | Migrate art + room pieces 5.7→5.8, reclassify halls, Z=0 validation, stairs for the `1_*` set | E2, E6 |
| **E8 — Runtime systems** | Nav invokers, wall nav exclusion, AI, geometry instancing, perf pass | E3, E7 |

The §5 port checklist maps onto these one-to-one and can be lifted straight into story titles.

### 10.5 Agent ownership

| Domain | Agent |
|---|---|
| ADR sign-off, C++/Blueprint boundary | `technical-director` |
| Solver architecture, C++ implementation | `lead-programmer`, `gameplay-programmer` |
| Engine APIs, 5.8 specifics, module setup | `unreal-specialist`, `engine-programmer` |
| Room/generator Blueprint shells, room contract | `ue-blueprint-specialist` |
| Editor widgets (baker, room builder) | `tools-programmer`, `ue-umg-specialist` |
| Room library, cell-grid authoring, layout feel | `level-designer`, `world-builder` |
| Geometry instancing, mesh merging, catalogues | `technical-artist` |
| Enemy AI, navigation | `ai-programmer` |
| Draw calls, nav invokers, generation timing | `performance-analyst` |
| Determinism tests, seed regression suite | `qa-lead` |

### 10.6 Guardrails for the agent team

**Settled — do not reopen.** The twelve ADRs in §10.2, and the four "already solved in Unity" call-outs in §3. These are backed by a shipped implementation, not speculation. If an agent proposes re-deriving one, point it at §9.

**Do not build.** §9.3 lists features the Unity project surfaced but never implemented — `LayoutStyle` Grid/Organic/Corridor, `branchingFactor`, `deadEndCount`, `secretRoomCount`. They are UI fields wired to nothing. A control that does nothing is worse than no control; don't let a completeness-minded agent reconstruct them.

**Verify, don't assume.** §9.4 — the 400 uu / 600 uu cell and tier sizes are *derived* from the Unity metre values and have **not** been checked against the Fantastic Dungeon Pack's module pitch. That check must happen in E1, before any room is authored, because it is far more expensive to change afterwards.

**Distinguish verified from inferred.** §1's variable and asset inventory was extracted from the v1 `.uasset` name tables and is reliable. The event *wiring* in §1.4 is reconstructed from names plus the tutorial and is marked *(inferred)*. Anything depending on exact v1 node order must be confirmed against the open project, not this document.

**Two known bugs to fix, not copy.** The unrotated `boundsOffset` and the single-exit spine retry — both §9.1(d) and the "known gap" note in §9.1.

**Collaboration protocol.** CCGS agents ask before writing and require approval for multi-file changesets. That is the right default here: E3 in particular is a port with real subtleties (rotation-aware bounds, cardinal re-snap, backtrack budget restore), and a silent rewrite loses them.

### 10.7 Where this file goes

Copy it into the new project as **`docs/handoff-from-v1.md`** and reference it from `design/gdd/systems-index.md`. It is the provenance record for the four GDDs and twelve ADRs — worth keeping after transcription, since it carries the *why* (including the v1 failure modes) that the artifacts themselves will compress away.

Keep the two source projects reachable for the duration of the port:
- `E:\UE5 Projects\UE5_Procedural_Level_Generator` — v1 Blueprint graphs, for anything marked *(inferred)*
- `E:\Unity\Unity_Procedural_Level_Creator` — the reference implementation being ported

---

## Appendix A. Name map (v1 → suggested v2)

| v1 | v2 |
|---|---|
| `Procedural_Dongeon` (module) | `ProceduralDungeon` |
| `MaxDongeonTime` | `MaxPlacementAttempts` (semantics change — §3.3) |
| `numberOfRooms` | `RemainingRoomBudget` |
| `RoomList` / `BaseRoomList` / `UniqueRoomList` | single `RoomTable` (§3.5) |
| `OverlapList` | *(removed — no physics test)* |
| `Check For Dongeon Complete` | `TickGenerationWatchdog` |
| `CloseOpenDoorway` | `CapUnusedExits` |
| `AddOverlapingRoomsToList` | *(removed — replaced by `FitsAt()`)* |
| `SpawnPickupAtLocation` / `Spawn Enemies At Location` / `Spawn Exit` | `SpawnAtFloorPoints` (§4.4) |
| `FloorSpawnItemLocation` | *(local variable, not actor state)* |
| `DoorList` | *(removed — derived from `ExitPoint::bIsConnected`, §9.1(c))* |
| `ExitsList` | *(removed — derived from `!bIsConnected && !bIsSealed`)* |
| — *(no equivalent)* | `FPlacementFrame` backtrack stack (§9.1(b)) |
| — *(no equivalent)* | `FRoomExit` — stateful socket, from Unity `ExitPoint` (§9.1(c)) |
| — *(no equivalent)* | hall connector pool (§9.1(e)) |
| — *(no equivalent)* | generation manifest (§9.1(g)) |
