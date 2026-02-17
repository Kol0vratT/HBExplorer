# HBExplorer

HBExplorer is an in-process Unity IL2CPP explorer overlay for DirectX 11 games.
It hooks `IDXGISwapChain::Present` via kiero, renders an ImGui UI, and exposes runtime scene/object inspection with editing and method invocation tools.

## Screenshot

![HBExplorer UI](https://i.postimg.cc/mkxkG7FP/image.png)

Suggested path: `docs/images/hbexplorer-ui.png`

## Core Features

- DirectX 11 Present hook (`kiero` + `MinHook`) with ImGui render loop.
- IL2CPP integration via `IL2CPP_Resolver`.
- Toggle overlay visibility with `F1`.
- In-game Object Explorer + Inspector split layout.
- Scene hierarchy browser with:
  - scene selector
  - search filter
  - include inactive toggle
  - configurable refresh interval (250-5000 ms)
  - manual/auto refresh
- Scene loader (Single/Additive by scene name).
- Object search tab:
  - class filter
  - name contains filter
  - quick selection into inspector
- Inspector capabilities:
  - object metadata (name, type, address, scene handle, instance ID when available)
  - active state toggle
  - layer editing (clamped 0-31)
  - transform editing (position/rotation/scale) with apply/reload
- Component introspection:
  - fields panel
  - methods panel
  - back navigation history for referenced-object jumps
- Field editing supports these IL2CPP value types:
  - `bool`, `int/uint`, `short/ushort`, `sbyte/byte`, `float`, `double`, `char`, `string`
- Static and instance field handling.
- Reference preview for object/class/array-like fields, with inspect jump for `GameObject`/`Component` references.
- Method metadata and invoke tools:
  - return type + signature preview
  - method/invoker pointers (RVA shown when inside `GameAssembly`)
  - invocation via `il2cpp_runtime_invoke`
  - supported argument types match editable primitives/string
  - invoke guard for oversized argument lists
- Runtime logging to `HBExplorerLogs.txt` in the target process directory.

## Requirements

- Windows
- Unity IL2CPP target application
- DirectX 11 renderer in target
- Visual Studio C++ toolchain (project includes VS v142/v145 configurations)
- DirectX SDK (June 2010) installed at default path used in project settings

## Build

1. Open `HBExplorer.sln` in Visual Studio.
2. Select `x64` (`Debug` or `Release`).
3. Build the solution.
4. Output is a DLL (`HBExplorer.dll` by default project naming rules).

## Usage

1. Launch the target Unity IL2CPP DirectX 11 application.
2. Inject the built `HBExplorer.dll` into the target process.
3. Wait for the in-game injection toast message.
4. Press `F1` to open/close the explorer.
5. Use:
   - `Scene Explorer` tab for hierarchy browsing and scene loading.
   - `Object Search` tab for filtered object lookup.
   - `Inspector` window for object/component/field/method operations.

## Project Layout

- `HBExplorer/main.cpp`: hook setup, ImGui lifecycle, hotkeys, render loop, logging.
- `HBExplorer/UExplorer.hpp`: explorer state, scene/object cache, inspector UI, field/method tooling.
- `HBExplorer/includes.h`: shared includes and compile-time Unity version switch.
- `HBExplorer/imgui/`: Dear ImGui sources + DX11/Win32 backends.
- `HBExplorer/kiero/`: kiero + bundled MinHook.

## Notes

- This project is intended for reverse engineering, debugging, and research workflows on software you are authorized to analyze.
- Cursor API hooks are present in code but currently disabled in initialization (`InitCursorApiHooks` call is commented).
