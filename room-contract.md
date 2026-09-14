last mattered: 2026-09

# The room contract

What `Master_Room` requires, how the bake satisfies it, and the C++ the authoring panel rests
on. Everything below was read out of the assets, not inferred.

## `Master_Room`'s structure

```
DefaultSceneRoot     SceneComponent   (0,0,0)
├── Arrow            ArrowComponent   (1000,0,0)
├── GeometryFolder   SceneComponent   (0,0,0)
├── Overlap Folder   SceneComponent   (0,0,0)
│   └── Overlap_Box  BoxComponent     (1000,0,-50) extent 32, scale 30x30x1
├── Exits Folder     SceneComponent   (0,0,0)
└── FloorSpawnPoints SceneComponent   (0,0,0)
```

`Overlap_Box` is `objectType 14` = `ECC_GameTraceChannel1` = **RoomOverlap**, confirmed. At
extent 32 and Z −50 it is a thin footprint slab *under* the floor, not a room-filling volume.

## How a child room extends it

Added components are roots of their own SCS and name their real parent through
`ParentComponentOrVariableName`. **Miss that and everything lands at the actor root, where the
generator's exit scan does not look.**

```
exit arrows  → Exits Folder     (inherited)    entrance arrow at (0,0,0)
spawnpoints  → FloorSpawnPoints (inherited)
floorPieces  → GeometryFolder   (inherited)
```

Both inherited components are overridden per room in the `InheritableComponentHandler`, not in
the child's SCS:

```
Arrow        → room centre,   (2000,0,0) on a 4000-long room
Overlap_Box  → (centre, 0, -50), scale floor(half/32) − 1
```

That scale rule was derived, not guessed. `1_Room1` ships 61 where 2000 wants 62.5, `1_Hall1`
ships 30 where 1000 wants 31.25, and the bake's own 7x7 room produced 42 where 1400 wants
43.75. All three fit the rule; a constant inset fits none of them.

## Kit sets — the C++ the panel rests on

| Function | What it is for |
|---|---|
| `GetInheritedPieceDisplayName(Recipe, Slot)` | what a slot WOULD resolve to. Reads the kit only — never a chamber, so it cannot hand the panel a value to freeze |
| `GetInheritedPieceLabel(Recipe, Slot)` | the combo entry meaning inherit: `(inherit - Wall_01_E_straight_large)` or `(inherit - none)` |
| `SetRoomKitSet(Recipe, KitSet)` | the only door onto `KitSet`, which is `BlueprintReadOnly`. Null is legal |
| `GetKitSetLabels()` | every kit set in `/Game`, built-in census first |
| `KitSetForLabel(Label)` | label to asset; null for the built-in entry and for unknown labels |
| `LabelForRoomKitSet(Recipe)` | asset to label, DERIVED through the same rule the scan uses |

Slot names mirror `GetChamberPieces`' outputs: `Wall`, `CornerNW`, `CornerNE`, `CornerSE`,
`CornerSW`, `Floor`, `CeilingMesh`. All four corners answer with the kit's single `Corner`.

The two bracketed labels — `(inherit - ...)` and `(built-in census)` — carry a load-bearing
guarantee: **an asset name cannot contain a bracket**, so neither can ever collide with a real
piece or kit however the libraries grow.

## How the panel applies a kit

On Refresh, not on selection. `RefreshCombos` fills and selects the picker; `PushUI` reads it
back; `DoRefresh` calls `RefreshCombos` so the `(inherit - ...)` labels, baked in at fill time,
follow a kit change. This matches the rest of the panel — the piece combos have no change
handlers either.

Immediate-apply was attempted and abandoned. The reason is a hard MCP limit, recorded in
`failures.md`.

## Content facts that constrain authoring

**No room in this project has a ceiling** — not `Master_Room`, not any of the three hand-built
pieces. But `DA_PieceTable_Ceiling` does offer three meshes (`MOD_Floor_01_O_straight_med` and
`_hole`, flipped floors). So `UDungeonKitSet::CeilingMesh` ships empty because no room is
evidence for a default, **not** because there is nothing to pick.

**Wall families diverge.** The hand-built rooms use `MOD_Wall_01_M_*` (pivotMiddle) meshes with
`MOD_Floor_01_E_*` floors. The ported kit uses `BP_COMP_Wall_01_E_*` actors with an `_O_`
floor. Different construction systems, and the census proved `_E_` is the only family that can
close a room — so **authored rooms will not look like the existing ones.** Unresolved by
design.

**No `DA_KitSet_*` asset exists yet.** The picker offers exactly one entry, correctly.
