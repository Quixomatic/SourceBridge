# Changelog

All notable changes to SourceBridge will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] - 2026-02-11

### Fixed
- FGD parser infinite loop causing UE Editor to hang at 73% on startup (rewritten based on ValveFGD reference)
- VMF plane winding order producing invalid solids in Hammer++ ("no faces found on solid")
- Removed debug logging from SourceBridgeModule startup

## [1.0.0] - 2026-02-11

### Added

#### Core Geometry (Phase 1)
- VMF KeyValues writer with nested block support
- UE brush to VMF solid exporter with scene traversal
- Convexity validation for brush geometry
- Test box room generator for quick validation

#### Materials (Phase 2)
- Material mapping from UE materials to Source VMT shaders
- UV coordinate export with proper axis alignment
- VMT file generation (LightmappedGeneric, UnlitGeneric, WorldVertexTransition)
- Texture export to TGA with VTF conversion via vtfcmd.exe
- Surface property database (surfaceproperties.txt parsing)

#### Entities (Phase 3)
- Point entity export (spawns, lights, env_soundscape)
- Brush entity support (func_detail, func_door, triggers with geometry)
- FGD parser for entity schema validation
- Source entity actor system with custom UE actors per entity class
- Soccer-specific entities (game_round_win, func_nogrenades)
- Spectator spawn actors
- Visual spawn indicators (colored arrows and capsules)
- FGD-driven detail panel with dynamic property widgets
- Custom entity detail panel with validation

#### Entity I/O (Phase 4)
- Entity I/O connection system (Output → targetname,Input,param,delay,refire)
- I/O visual wires in the editor viewport
- Entity palette for browsing and spawning Source entities

#### Tag Configuration (Phase 5)
- Tag-driven entity configuration on UE actors
- Trigger volume export with touch/damage/push types

#### Model Export (Phase 6)
- SMD exporter for static meshes with bone weights
- Skeletal mesh export with bone hierarchy
- Animation export to SMD sequences
- QC file generation for studiomdl compilation
- Physics collision extraction and convex decomposition
- Mesh-to-brush conversion for simple geometry

#### Compile Pipeline (Phase 7)
- Headless compile pipeline (vbsp → vvis → vrad)
- Steam libraryfolders.vdf parsing for auto-detecting game paths
- Full export pipeline (export → validate → compile → package)
- Distributable packaging (copy BSP + assets to game directory)
- Compile time estimation heuristics
- Console commands: ExportTestBoxRoom, ExportScene, CompileMap, ExportModel, FullExport, Validate

#### Advanced Geometry (Phase 8)
- Displacement surface export from UE Landscapes
- Skybox export (3D skybox with sky_camera)
- Prop export (prop_static, prop_dynamic, prop_physics)
- Vis optimization (hint/skip brushes, area portals, func_viscluster)
- Water volume export with shader selection

#### Editor UI (Phase 9)
- Editor toolbar with SourceBridge dropdown menu
- Export settings panel (target game, output path, compile options)
- VMF preview panel with text output
- Compile progress UI with notifications
- Export validation with error/warning reporting

#### Build Compatibility
- Full UE 5.7.3 compile compatibility
