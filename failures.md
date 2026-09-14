last mattered: 2026-09

# Failures

Every entry here cost time to discover at least once. Several cost it twice.

## Build and test

- Do not use `Build.bat`. The engine path contains a space and the batch wrapper mangles it. Call `UnrealBuildTool.exe` directly.
- Do not build with the editor open. Live Coding holds the DLL and the link fails.
- Do not run automation against the Game target. It links, then crashes in `BufferReader.h` at startup on uncooked content, before the automation harness loads. That reads as a test failure and is not one. Use the Editor target.
- Do not trust Live Coding as a build. It patches function bodies and new `UFUNCTION`s without complaint, but refuses struct-layout changes and new `UCLASS`es — and its refusal message says nothing useful. Close the editor and build to see the real error.
- Do not trust a headless test run that follows a failed build. It executes the stale DLL and means nothing. This nearly reported a false pass once.
- Do not trust a new automation test added by a non-first Live Coding patch. Live Coding registers new tests only on a module's FIRST patch per editor session. A later one reports `Result: Success`, but `ListTests` omits it and `RunTests` returns `total: 0` rather than an error. Touching the file and recompiling does not help. A test that will not appear is not a passing test — close the editor and build.

## MCP

- Do not resurrect `mcp-unreal`, the third-party Go bridge (`github.com/remiphilippe/mcp-unreal`). Its docs index is a boltdb store taking an exclusive file lock, and a leftover `mcp-unreal.exe` held that lock across sessions, blocking every new spawn before it could answer `initialize` — a 30 s handshake timeout. Two sessions blamed the editor being open; it was never that. Use Epic's in-editor server.
- Do not call a toolset tool by its full name. `bEnableToolSearch=True`, so `tools/list` returns only `list_toolsets`, `describe_toolset` and `call_tool`. Everything goes through `call_tool` with `toolset_name` + `tool_name` + `arguments`. Direct calls fail with "Unknown tool".
- Do not pass `ProceduralDungeon.RoomAuthoring` to `RunTestsByFilter`. It enables 8855 tests, the whole engine suite. Use `ListTests` then `RunTests` with explicit names.
- Do not omit `tagFilter` from `ListTests`. The schema calls it optional and it is not. Pass `""`.
- Do not expect `GetTestResults` to return `AddInfo` output. It returns only errors and warnings. `AddInfo` goes to `Saved/Logs/Procedural_Dongeon.log` — grep `LogAutomationController`.
- Do not look for a Blueprint-inspection toolset. There is none. To read a Blueprint's structure, write an automation test that loads it and dumps what you need; `MasterRoomContractTests.cpp` is the working example and is how the whole `Master_Room` contract was established.
- Do not write a node type id you read back from `read_graph_dsl`. Ids are ambiguous and the wrong one builds silently: `SelectOption` reads back as `SwitchActor|SelectOption`, an unrelated engine function on `ASwitchActor` with completely different pins, and `Behavior|GetValue` has five overloads resolving to Radial Slider, not Spin Box. Verify with `find_node_types` and `get_node_type_pins`. The `Class|<Type>|<Fn>` form is unambiguous.
- Do not pass member-call arguments positionally. The reader prints them without `self`; the writer binds the first positional argument TO `self`. Use keyword form — `(CallFunction|ComboClear :Combo Combo)`, not `(CallFunction|ComboClear Combo)`. The reader is not even self-consistent: it printed `self` in `FillAttachCombo` and omitted it in `FillCombo`.
- Do not use `(== a b)` in the DSL. It picks an overload by guess and fails on strings. Use `Utilities|String|IsEmpty` or `Utilities|String|EqualExactly(String)`.
- Do not rewrite `EUW_RoomAuthor`'s EventGraph with `write_graph_dsl`. It carries designer-bound widget events — `(event OnClicked(SaveButton) ...)`, `(event OnSelectionChanged(AttachCombo) ...)` — that the writer cannot reproduce, so the rewrite silently unbinds Save, Load and Bake. There is no safe MCP route to a new designer-bound widget event at all: UMG widgets are not `ActorComponent`s, so `add_component_bound_event` refuses them. That is why the kit picker applies on Refresh rather than on selection.
- Do not change a Blueprint function's signature without rewriting its callers in the same sitting. `add_function_param` on `FillCombo` made the body writable but left seven stale call nodes in `RefreshCombos`, and the next `write_graph_dsl` failed with "Could not find a pin for the parameter Slot".

A failed `write_graph_dsl` leaves the graph untouched, which is the one merciful part of the above.

## Git

- Do not commit without reading `git status`. An MCP call that touches an asset marks it dirty and the editor writes it out on close or autosave, so a `.uasset` shows as modified with nothing in the session obviously touching content. A single `compile_blueprint` on `EUW_RoomAuthor` did it once, as a pure re-serialise with no semantic change. Three accidental commits of baked rooms landed this way at 4.5 MB of LFS each. The answer is usually `git checkout --` on the asset you did not mean to change.
- Do not widen the baked-room ignore rule to the whole folder. `DA_RoomRecipe_*` sits in `Content/RectDungeon/Rooms/` by design and is authored SOURCE — a blanket rule quietly stops tracking the rooms themselves. The rule names `BP_Room_*` and `DA_Room_*` deliberately; `DA_Room_*` does not match `DA_RoomRecipe_*`.
- Do not clone without Git LFS installed. `*.uproject` is LFS-tracked in `.gitattributes`, so the clone gets a pointer file and cannot open the project.
- Do not read a silent push failure as a network or permission fault. When the Git Credential Manager entry goes stale, every push fails *without prompting*. GCM stores the github.com credential under username `jlcorbin`, not `jasoncorbin`. To see what is stored, feed `protocol=https` and `host=github.com` on stdin to `git credential-manager get`.

## Authoring tool

- Do not drop the chamber-free backstop from a bake-refusal fixture. It exists so no test can drive a real bake even if the guard it exercises stops firing. It also means guard ordering matters: the no-exits guard was unreachable until it moved above `ValidateRecipe`, because the backstop refused first.
- Do not try to automate the bake's success path. It needs a live authoring world with generated actors, and a test that writes into the room library then fails mid-run is how a throwaway asset gets committed. Run a bake by hand, then run `Contract.BakedRoomsSatisfyTheMasterRoomContract`, which checks whatever the library holds.
- Do not assume a room's inherited piece slots survive a repaint unexamined. `RepaintPanel` ends with `PushChamber`, and `PullChamber` once called `SelectOption(combo, "")` for an inherited slot; `SelectOption` could not find `""`, did nothing, and left index 0 — a real piece — selected, which `PushChamber` wrote back as an explicit override. Opening the tool converted all seven inherited slots into hard overrides. Fixed by seeding each combo with `GetInheritedPieceLabel`, but the shape of the bug is easy to reintroduce.
