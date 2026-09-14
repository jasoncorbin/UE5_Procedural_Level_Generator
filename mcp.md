last mattered: 2026-09

# MCP

## Which server is which

Two different products, easy to confuse because both say "unreal MCP":

| Name | What it is | Status |
|---|---|---|
| `unreal` | **Epic's official server.** Engine plugin `ModelContextProtocol` (Experimental) + `MCPClientToolset`, shipped with UE 5.8. | In use |
| `mcp-unreal` | **Third party**, `github.com/remiphilippe/mcp-unreal`. Standalone Go binary, Apache-2.0, targets UE 5.7. | Dead. Do not resurrect |

The server name in `.mcp.json` is `unreal`. Anything still saying `mcp-unreal` refers to the
third-party bridge.

## Epic's server

Shipped with UE 5.8 as `ModelContextProtocol` — "Anthropic MCP server implementation for
Unreal Engine", by Epic. It is an **Experimental** engine plugin, so its surface can move
between engine versions. Already enabled in this project with
`bAutoStartServer=True`, port 8000, path `/mcp`, in
`Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`. The editor serves it itself;
there is no separate process and nothing to install.

```json
{ "mcpServers": { "unreal": { "type": "http", "url": "http://localhost:8000/mcp" } } }
```

It follows that **the endpoint is only up while the editor is running.** A connection refusal
means the editor is closed, not that anything is broken.

The third-party bridge this replaced is gone and must not come back — see `failures.md` for
why it failed, because it was misdiagnosed twice.

## Dispatch

`bEnableToolSearch=True`, so `tools/list` returns only `list_toolsets`, `describe_toolset` and
`call_tool`. Everything goes through `call_tool` with `toolset_name` + `tool_name` +
`arguments`.

21 toolsets. The useful ones here: `AutomationTestToolset` (run tests in the live editor),
`LiveCodingToolset`, `UMGToolSet`, `PCGToolset`, `EditorToolset`, `SlateInspectorToolset`.

`UMGToolSet` manipulates the widget TREE only. Every graph edit goes through
`editor_toolset.toolsets.blueprint.BlueprintTools` instead — `read_graph_dsl`,
`write_graph_dsl`, `find_node_types`, `get_node_type_pins`, `add_function_param`,
`compile_blueprint`. Those tool names were always Epic's; the old bridge borrowed them.

## The graph DSL does not round-trip

`read_graph_dsl` emits forms `write_graph_dsl` refuses, and worse, forms it accepts and
misinterprets. **Read, verify every node type with `find_node_types` and `get_node_type_pins`,
write, then read back.** The specific traps are in `failures.md`; all three of them produce
silently wrong graphs rather than errors.

## Session note

A Claude Code session started before `.mcp.json` changed keeps retrying the dead stdio server.
Restart to pick it up, or drive the HTTP endpoint with `curl` directly.

`.claude/settings.local.json` carries `enableAllProjectMcpServers: true` and nothing else of
consequence. That one flag enables everything `.mcp.json` defines, which is Epic's `unreal`
server and nothing else. The file's former `mcp-unreal` entries were stripped 2026-09-13.

The file is untracked — ignored globally by `**/.claude/settings.local.json` — so a fresh clone
will not have it, and Epic's server still loads without it.
