# Plugin host: the MCP server as a plugin DLL

Status: planned 2026-10-19, not started. Nothing in this document has been built or verified unless a section says so.

## Goal

The game loads exactly one rules DLL. Today that DLL is the Local MCP Server mod, so every other change to the game's rules (city-state explorer fix, friendship rules, calmer AI) either goes into the MCP mod or does not exist. That mixes mods of different intent in one binary and makes it impossible for anyone else to add their own.

The goal is one **host DLL** that loads any number of **plugin DLLs**, each its own mod with its own intent:

- the MCP server is a plugin, not part of the host;
- rules fixes are their own plugins (or plain Lua mods on the host's hooks);
- other developers can write plugins against a documented SDK;
- an existing DLL mod can become a plugin: each engine function it edited becomes a patched copy inside the plugin, with no change to the host.

The rule that outranks everything else: **the host never contains the MCP server, and the MCP plugin never contains rules changes.**

## Shape

```
Host DLL  (replacement CvGameCore_Expansion2.dll, one per game)
  = whoward69's DLL-VMC v97 fork + plugin loader + exported engine classes + hooks + function patching
  own GUID, own name, credits VMC, source on GitHub

Plugin DLLs  (ordinary mods: one folder each, with plugins/<Name>.dll inside)
  MCPServer.dll          the MCP server, its log window Lua, its tools
  <other>.dll            anything else that needs C++; Lua mods use VMC hooks instead
```

The host finds plugins through the game's own mod system: each plugin mod inserts one row into a `DllPlugins` table (file name, minimum host API version). At the point where VMC reads its own `CustomModOptions` table, the host reads `DllPlugins`, finds each file under the MODS folder, loads it and runs the handshake below.

## Why VMC as the host base

VMC already is a hook-provider DLL: about 60 groups of Lua `GameEvents`, Lua API extensions, switchable bug fixes, and the `CustomModOptions` convention that Workshop mods already use. Starting from it means rules changes that only need a hook can be plain Lua mods from day one, and the native plugin API inherits the same hook list.

Terms (whoward69's usage page): parts may be incorporated into a greater work that has its own original additions, with credit; no resale or financial gain; derivatives must use their own GUID. No formal licence exists; post in his CivFanatics thread before the first Workshop release.

VMC is built from the same 1.0.3.276 SDK source as our DLL, with a VS2015 solution wrapping a vs2010 project. We keep our VS2008 build (`build-dll.cmd`) and add VMC's extra files to our project.

## Host design

### 1. Exported engine classes

A plugin needs real access to players, units, cities, plots, deals, diplomacy, notifications and the globals (`GC`, `gDLL`). The host marks classes as exported with one macro on the class declaration (`CIV_HOST_API`), starting with the classes the MCP code uses and growing on demand. Plugins link against the host's import library.

What makes this safe: both sides are built by the same VS2008 compiler with the shared C runtime (`RuntimeLibrary=2`, `/MD`) and the same compile flags, so objects, strings and vectors cross the boundary unchanged. The flags must be byte-identical (the map-drawing crash of v40 came from one mismatched define); the SDK ships a property sheet that pins them.

Limits: a DLL may export at most 65,535 symbols; a few hundred classes is far below that, and the build prints the count. Exported classes lock plugins to the exact host build (next section).

### 2. Build ID and handshake

Every host build embeds a build ID (host version + git hash). The SDK the plugin is built against carries the same ID. At load the host calls the plugin's single exported entry point:

```
extern "C" bool CivPluginEntry(const CivHostInfo* host, CivPluginInfo* plugin);
```

The plugin fills in its name, version and the host build ID it was compiled against. Mismatch: the host does not load it, logs why, and shows an in-game notification naming the plugin and the two IDs. Never a silent skip.

### 3. Hooks

The loader gives plugins a small C interface: `RegisterHook(name, function, userdata)` and `Log(line)`. Hook signatures use engine pointers (`CvUnit*`, `CvPlayer*`), which is fine because the ABI is already locked by the exports. Hooks are additive: once shipped, a hook's meaning never changes; new ones get new names.

The first hook set is exactly what the MCP code needs today (inventory from `grep Claude` outside `Claude/`):

| Today's engine edit | Becomes |
|---|---|
| `CvGame.cpp` turn loop: a seat is activated like the local player | query `IsPluginControlledPlayer(player)` |
| `CvDllPlayer::IsHuman`, `CvDllPreGame::isHuman` | query `EngineSeesAsHuman(player, default)` |
| `CvPlayer::setAlive` → keep the dead slot open | query `KeepDeadSlotOpen(player)` |
| `CvUnit::UnitPathTo` six cancellation sites | notify `UnitOrdersCancelled(unit, reason)` |
| `CvNotifications::Add` | notify `NotificationAdded(player, message, summary)` |
| popup sites (wonder, great person, city captured, goody, golden age, natural wonder, tech, era, barbarian camp, city-state greeting) | notify `PopupRaised(player/team, type, data…)` |
| `CvDiplomacyRequests` request and deal interception | query `InterceptDiploRequest(...)`, `InterceptDealRequest(...)` |
| `CvDealClasses` deal resolved | notify `DealResolved(...)` |
| `CvUnitMission` skip for seats | query `IsPluginControlledPlayer` (same as above) |
| `CvGame::update` → `ClaudeBridge::Tick()` | notify `GameCoreUpdate()` (VMC has one already) |
| `CvDllContext` shutdown, `DllMain` crash-handler removal | plugin `Shutdown()` lifecycle call |
| `CvLuaGame.cpp`: a dozen `Game.*` methods for the log window | `RegisterLuaMethods(lua_State*, table)` called when the host registers its own |

Rules fixes currently inside the DLL that are **not** MCP:

| Fix | Where it goes |
|---|---|
| captured cities keep their seen-by flags (`CvPlayer::acquireCity`) | host, as a VMC-style switch (`MOD_BUGFIX_...`), default on |
| seat starting units re-dealt as a human's | MCP plugin (it is about seats) |

### 4. Function patching

Hooks cover the needs we know. They do not cover what most DLL mods actually are: a diff against the SDK, lines changed inside existing functions (a growth formula, a condition in the combat code, an AI weight). Gaia's Core Mod and whoward's own fixes are all of this kind. Asking the host for a hook at every such spot would turn it into a list of everyone's edits, each one a contract kept forever.

So the loader offers a third tier: a plugin can patch any exported host function at load time.

```
Patch(target, replacement, &original, PATCH_WRAP | PATCH_REPLACE)
```

The replacement runs instead of the target; `original` is a trampoline the replacement may call to run the real function. Calling it before or after the plugin's own code gives the usual "run something before / after / instead" shapes with one call. Member functions are patched with a free function that takes the object pointer first; the SDK ships a macro for that. Targets are named through the import library, never by offset, so a patch needs exactly what a hook needs when the host changes: a rebuild against the new build ID, nothing more.

Converting an existing DLL mod is then mechanical: for each function the mod edited, copy the host's version into the plugin, apply the edit, register it as a replacement. The SDK headers expose private members to plugin code (a define in the property sheet), because a copied function needs the access the original had.

Rules the loader enforces:

- **Wrap stacks, replace does not.** A `PATCH_WRAP` promises to call `original`; any number of plugins may wrap one function, each wrapping the one before. A `PATCH_REPLACE` does not call `original`. A second replace on a function that already has one is refused, logged with both plugin names, and shown as an in-game notification. Never a silent last-one-wins.
- **Every patch is recorded** with its plugin name, so a crash log can say which plugins touched the function that died.
- **Inlined calls are not caught.** A patch catches calls that enter the function; calls the compiler inlined inside the host do not. Where a small function matters as a patch target, the host marks it non-inlinable, on request, and the SDK lists the functions known to be inlined.

Detouring itself is a solved problem; MinHook (BSD-2, small C library, 32-bit x86) is the candidate, to be confirmed in P0 against the VS2008 build.

### 5. Save games

The host's DLL GUID is what saves record. Plugins keep no state in saves today (seat labels live in the game's player nicknames); if one ever needs it, add a `SaveData(stream)`/`LoadData(stream)` pair keyed by plugin name. Not needed for the MCP plugin.

## Plugin SDK

A folder shipped with each host release (also the thing other developers download):

- `include/` the host's headers (the engine's, with the export macro resolving to import);
- `lib/CivHost.lib` import library;
- `CivPlugin.vsprops` the exact compiler and linker settings of the host build;
- `build-id.h` the host build ID;
- `plugin-template/` a minimal plugin that registers one hook and logs a line;
- `include/CivPatch.h` the patch call and the member-function macro;
- `inlined-functions.md` the exported functions a patch cannot catch at every call site;
- `README.md` how to build, how the mod folder must look, how the `DllPlugins` row is written, how version mismatch shows up.

## The MCP plugin

`Claude/` (37 files, ~11,800 lines) moves to its own repository folder, `MCPServerPlugin/`, and becomes `MCPServer.dll`. Internally almost nothing changes: the HTTP server, tools, bridge, seat logic, map drawing, playback release and crash handler are already self-contained. The work is:

1. replace each engine edit with a hook registration from the table above;
2. move the `CvLuaGame.cpp` methods into the plugin and register them through `RegisterLuaMethods`;
3. keep the no-PCH drawing file's two defines in line with the SDK property sheet;
4. package: `MCPServer.dll` under `plugins/`, `LLMLog.lua/.xml`, the `DllPlugins` XML row, the log window's UI entry point, the mod description.

The seat/playback code pokes the game executable's memory by pattern scan; that stays in the plugin and needs nothing from the host.

## Versioning and releases

- Host releases on GitHub, tagged, each with its SDK zip and build ID. Workshop is optional for the host; a Workshop host would auto-update and break plugins, so the Workshop page (if any) must say so and point to GitHub.
- A plugin release names the host build ID it was built for. People match versions by reading two numbers; the handshake catches mistakes.
- Hooks and exports are additive; nothing shipped is renamed or changed in meaning.

## Phases

**P0 — proof of concept (days).** Against the *current* DLL, before touching VMC:
- export ~10 classes with the macro; build a toy plugin that reads a player's gold and a unit's position through them;
- load it from a MODS folder found through a `DllPlugins` row; handshake with a deliberately wrong build ID to see the refusal path;
- confirm strings and vectors cross the boundary (allocate in one DLL, free in the other);
- patch one host function from the toy plugin (say the gold getter), call the original through the trampoline, and see the changed value in game; then load a second toy plugin that replaces the same function and see the refusal.
Exit: the toy plugin runs in a real game, its patch shows in play, and both refusal paths show their notifications.

**P1 — the host.** Fork VMC v97 into the repo (own GUID, credits), bring our build scripts over, add the loader, the patch API, the export macro on the classes MCP needs, the hook set above, and the acquireCity fix as a switch. Exit: the host builds, loads a game, VMC's events fire, no MCP code inside.

**P2 — MCP as a plugin.** Move `Claude/` out, wire hooks, Lua registration, packaging. Exit: every existing tool and the log window work exactly as in v43, verified by playing a few turns with a model and reloading a save.

**P3 — the rest.** Explorer fix, friendship/denounce rules and calmer AI as Lua mods on VMC hooks where one exists, native plugins where not; the calmer-AI fix is the natural first patch plugin, since it replaces one diplomacy function. A plugin-writing guide. Post in the VMC thread; first GitHub release of host + SDK + MCP plugin.

## Risks

- **ABI lock.** Every host build needs plugin rebuilds. Accepted; the build ID makes it a loud failure.
- **No isolation.** A plugin crash crashes the game, same as today.
- **Export count.** Watch the 65,535 limit; print the count in the build.
- **Load timing.** Plugins must load after the database is ready and before the first turn; the VMC options-read point is the candidate, to be confirmed in P0.
- **Lua registration.** The host registers Lua methods per context; the plugin's registration must run for each new context the same way. Verify in P2.
- **Patch conflicts.** Two plugins replacing one function is refused by rule; two plugins wrapping one function in an order that matters is not detectable. Document wrap order as load order and leave it there.
- **Inlined calls.** A patch may miss call sites the compiler inlined. The P0 toy must include one such case so the gap is seen, not assumed.
- **VMC behaviour changes.** VMC's bug fixes and features are switches; the host ships with the same defaults VMC does, and our own settings are a separate decision.

## Not in scope

- Stacking with other replacement DLLs (Community Patch etc.). Nothing can do that.
- Multiplayer. VMC's Lua events are not MP-safe and neither is the MCP server.
- A plugin registry or version-matching service. Two numbers and a handshake are enough.
