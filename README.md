# SourceBridge - Unreal Engine to Source Engine Export Plugin

An Unreal Engine 5 editor plugin that exports maps and assets to Source engine formats. Build everything in UE with modern tooling, export to Source, compile via CLI, play in CS:S. Full Hammer replacement.

## Why?

Hammer is a 20+ year old editor. Source engine formats are frozen forever. This plugin bridges a modern editor to a frozen target - once it works, it works until the end of time.

## Architecture

```
Unreal Engine Editor
  └── SourceBridge Plugin (C++)
        ├── MAP:      UE Brushes  → .vmf → vbsp/vvis/vrad → .bsp
        ├── MODEL:    UE Meshes   → .smd → studiomdl      → .mdl
        ├── MATERIAL: UE Textures → .tga → vtfcmd          → .vtf + .vmt
        └── AI (opt): Claude → MCP → UE → Export → Source
```

## Pipelines

### Map Export
UE BSP brushes export as VMF solids. Entities export as Source entities with full I/O. Lights convert to Source light entities. One-click compile via CLI tools (vbsp → vvis → vrad) produces a playable .bsp.

### Model Export
UE meshes export to SMD format with auto-generated QC compile files. CLI compile via studiomdl produces .mdl + .vvd + .vtx + .phy files.

### Material Export
UE textures export to TGA/PNG, convert to VTF via VTFCmd/VTFEdit-Reloaded, with auto-generated VMT shader definitions.

### AI Pipeline (Optional)
Existing Unreal MCP servers allow AI-driven scene creation. Combined with this plugin, you can describe a map in natural language and get a playable .bsp.

## Requirements

- Unreal Engine 5.4+ (via Epic Games Launcher)
- Visual Studio 2022 with "Game development with C++" workload
- Source SDK tools (vbsp, vvis, vrad, studiomdl) - included with CS:S via Steam
- Node.js (for MCP server, optional)

## Project Structure

```
SourceBridge/
  Source/
    SourceBridge/
      Private/              # C++ implementation
      Public/               # C++ headers
  Content/                  # UE assets (entity placeholders, tool materials)
  Resources/                # Plugin icon, metadata
  SourceBridge.uplugin      # Plugin descriptor
```

## Development

See `.plans/` for detailed phase-by-phase planning:

| Phase | Description |
|-------|-------------|
| 1 | Core geometry export (BSP brushes → VMF) |
| 2 | Materials & textures (VTF/VMT pipeline) |
| 3 | Entity system (Source entities as UE actors) |
| 4 | Entity I/O (visual wiring → Source connections) |
| 5 | Lighting (UE lights → Source light entities) |
| 6 | Model export (mesh → SMD → MDL) |
| 7 | Headless compile pipeline (one-click build) |
| 8 | Advanced features (displacements, skybox, water) |
| 9 | Editor UX (validation, browsers, dashboards) |
| 10 | AI-driven creation via MCP |

Full breakdown: `.plans/.ue-plugin-breakdown/overview.md`

## Supported Source Games

- Counter-Strike: Source
- Garry's Mod
- Team Fortress 2
- Half-Life 2 / Episodes
- Any Source 1 game using standard formats

## License

MIT
