last mattered: 2026-09

# The generator port — not started

The room-authoring tool is done. **The runtime generator is not.** It is still v1's Blueprint
graph, with every design flaw below intact, and the Unity project at
`E:/Unity/Unity_Procedural_Level_Creator` already contains working solutions to four of them.

Section numbers are kept from `HANDOFF.old.md` because the cross-references point at each
other. §3 is the diagnosis; §9.1 is the fix, already written in C#.

Do §3.1 first. It is the keystone: §3.2, §3.3, §3.7, editor-time generation, determinism and
MCP-callability all become easy once layout is a pure function, and all stay hard while it
isn't. Then §4.2, room metadata baking, which the solver needs. Everything after is
independent.

---

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

### 3.11 🟡 Project hygiene — mostly resolved

Of the four items originally listed here, three are done: `Source/` is a real module
(`ProceduralDungeon`, correctly spelled, 22 files), and neither `Intermediate/` nor `Saved/` is
tracked in git any more. The `Dongeon` typo survives only in the project filename
`Procedural_Dongeon.uproject` and the `Procedural_DongeonEditor` build target, not in the
module.

Still true: `Config/DefaultGame.ini` says `ProjectName=Third Person BP Game Template`. It is
also listed in `backlog.md`.

---

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
