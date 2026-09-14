last mattered: 2026-09

# Build, test, drive

Prohibitions for everything here are in `failures.md`. Read them first; three of them cause
failures that look like something else.

## Build

Editor must be CLOSED.

```bash
"E:/UE4 Projects/_UE4/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
  Procedural_DongeonEditor Win64 Development \
  -project="E:/UE5 Projects/UE5_Procedural_Level_Generator/Procedural_Dongeon.uproject" -waitmutex
```

The natural rhythm is: leave the editor closed while working, open it to test.

## Tests

69 tests, 69 passing, 0 failing — verified headlessly against a real build on 2026-09-12.

```bash
"E:/UE4 Projects/_UE4/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:/UE5 Projects/UE5_Procedural_Level_Generator/Procedural_Dongeon.uproject" \
  -nullrhi -unattended -nosplash -nopause -NoSound \
  -ExecCmds="Automation RunTests ProceduralDungeon.RoomAuthoring;Quit" \
  -TestExit="Automation Test Queue Empty" -log -abslog=<path>
```

Then count `Result={Success}` and `Result={Fail}` in the log.

| Group | n | What it covers |
|---|---|---|
| `Tools` | 20 | display names, the interior arch, chamber edit/attach, save/load, kit-set override panel, the inherit vocabulary, the kit picker |
| `Openings` | 7 | corner rules, one-opening-per-side, Open/Merged spans |
| `KitSet` | 6 | census defaults, inherit, override, ceiling-empty, the null-kit fallback |
| `Attachment` | 5 | centring, normalisation, ascending parents, back sides |
| `Bake` | 5 | all five refusal paths |
| `Layout` | 5 | bounds, overlap, root-cause ordering |
| `Recipe` | 4 | reflection to pure widening, connection toggles |
| `SharedEdge` | 4 | face-to-face, corner-only, diagonal, partial overlap |
| `BakeFrame` | 3 | rebasing against a measured room, rigidity, every entrance |
| `Bounds` | 3 | shrink-wrap, origin normalisation |
| `Contract` | 3 | `Master_Room`'s shape, hand-built rooms, rooms the bake wrote |
| `Exits` | 3 | midpoint reachability, planner agreement |
| `Content` | 1 | every migrated asset loads |

`Tools.EveryListedKitSetResolvesAndRoundTrips` is vacuous today and logs that it checked
nothing — the project has no `DA_KitSet_*` asset for it to iterate. Authoring one is what
gives it work to do.

## Drive the tool from the console

Works in-editor and under `-ExecCmds`.

```
ProceduralDungeon.Author.Generate <recipe object path>
ProceduralDungeon.Author.Bake     <recipe object path>
```

Regenerate must run before Bake — the bake harvests what is standing in the level.

These were written because the widget's Bake button could not work: `PushUI` passed literal `0`
for both bounding dimensions and `SetRoomFields` clamps with `Max(1, ...)`, so every room
collapsed to 1x1 and a 7x7 chamber failed validation. Both pins are now connected to the spin
boxes `PullUI` already fills, so the button should work — **but no one has pressed it since the
fix.** The console path never touches `PushUI`, which is why it always worked.

## Live Coding through MCP

How four fixes landed in one session without closing the editor:

```bash
mcp.sh call_tool '{"toolset_name":"LiveCodingToolset.LiveCodingToolset",
                   "tool_name":"CompileLiveCoding","arguments":{}}'
```

It accepts new reflected `UFUNCTION`s — three across four compiles. It refuses struct-layout
changes and new `UCLASS`es. See `failures.md` for what its success and failure reports are
actually worth.
