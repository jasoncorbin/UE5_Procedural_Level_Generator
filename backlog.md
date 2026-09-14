last mattered: 2026-09

# Backlog

Nobody is doing any of this. None of it blocks. Roughly prioritised.

- **Open the authoring tool and look at it.** Nothing was verified by clicking. Every graph was
  read back and the widget compiles clean with warnings as errors, but no human has seen the
  panel since it changed. Check three things: the piece combos read `(inherit - <piece>)` on an
  un-overridden chamber, the KitSet row appears under RoomType, and the bounding spins survive a
  Refresh instead of collapsing to 1. If any of those is wrong it is a bug, not a completed
  item — it becomes the next entry here rather than being struck off.

- **Run a bake from the Bake button**, not the console. It should work for the first time now
  that `PushUI` pushes real bounds. Run
  `Contract.BakedRoomsSatisfyTheMasterRoomContract` afterwards — it checks whatever the library
  holds.

- **Author a `DA_KitSet_*` asset.** The kit picker offers one entry today and
  `Tools.EveryListedKitSetResolvesAndRoundTrips` is vacuous until one exists.

- **Geometry volume.** 966 SCS nodes and 1.1 MB per baked piece, times four pieces for a
  four-exit room. Blueprints get slow to open and compile around ~1000 SCS nodes, and this is
  per room in the library. The fix is instanced static meshes rather than one component per
  tile — a real change, not a tweak.

- **Floor spawn points include the perimeter.** 49 points for a 7x7 room is every tile; the
  original brief said *interior* tiles, so a point sits against each wall. One-line fix to
  inset by a tile if wanted.

- **`Config/DefaultGame.ini` still says `ProjectName=Third Person BP Game Template`.**

- **The 5.7 to 5.8 content resave is partial and still owed.** `1_Hall1` and two ThirdPersonMap
  actors were resaved incidentally in `03a7a0d`; `1_Hall2` and four
  `__ExternalActors__/ThirdPerson/Maps/ThirdPersonMap/` actors were swept into `00daced`. That
  moved the boundary rather than finishing the job — opening the editor still upgrades whatever
  5.7 packages it touches, so more will appear. A deliberate pass in its own commit is the
  right shape.

- **The bake's per-piece paths are not shown in the widget.** `OutSavedPaths` is wired to
  nothing. The status line names the folder and the count, so this is cosmetic.

- **Two throwaway recipes are tracked room source.** `DA_RoomRecipe_Room_New` and
  `_Room_New2`, committed in `00daced`. If they were genuinely throwaway, delete them in their
  own commit rather than leaving them to be mistaken for a library.

- **`L_RoomAuthoring.umap` was committed without anyone establishing what changed in it.**
  Modified since before 2026-09-08, swept into `00daced`. Committed rather than understood.

- **`magic` MCP server fails to connect** (API key reset). Unrelated to this work.
