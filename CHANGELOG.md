# Changelog

All notable changes to SourceBridge will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.2] - 2026-02-13

### Fixed
- Blue tint in Lit mode caused by TOOLS textures blocking light (NODRAW, CLIP, TRIGGER, SKYBOX, etc.)
- TOOLS-only meshes now have shadow casting and distance field lighting disabled at the component level
- TOOLS materials switched to Unlit+Translucent base so they don't interact with the lighting system
- Lit mode normals using broken centroid heuristic replaced with VMF plane normals (import and reload paths)
- Base materials missing Roughness=1.0 and Metallic=0.0 causing specular blue sky reflections
- Missing tangent data on imported proc mesh sections causing flat shading in Lit mode
- Save/reload persistence for props (UStaticMesh) and worldspawn brushes (StoredBrushData + PostLoad reconstruction)
- Texture z-fighting caused by csgRebuild running after import
- Texture sizes not reloading correctly on re-import

### Added
- Source Material Browser panel with texture preview thumbnails
- Persistent material and texture re-import on subsequent loads

## [1.1.1] - 2026-02-13

### Added

#### Map Authoring Workflow (Phase 15)
- Per-face material editing for brush entities (select face, apply material from picker)
- Brush entity spawning with default geometry (creates box brush on spawn)
- Shape overrides for spawned brush entities (box, wedge, cylinder, arch)
- Static mesh to brush conversion on export (convex decomposition)
- Worldspawn brush creation and Tie to Entity tools
- Export validation for brush entities and degenerate geometry

#### Full Round-Trip Material Pipeline (Phase 16)
- Central material manifest (USourceMaterialManifest UDataAsset) tracking all Source materials
- Persistent texture import: VTF decoded to UTexture2D assets (survive editor restart)
- Persistent material import: UMaterialInstanceConstant assets with proper base materials
- Manifest-first material mapping on export (correct Source paths, no more broken name-based mapping)
- MaterialAnalyzer: extracts textures, blend mode, two-sided flag from arbitrary UE materials
- Custom UE material export: TGA → VTF conversion with DXT1/DXT5 format selection and mipmaps
- VMT generation from material analysis (maps UE properties to Source VMT parameters)
- Lossless re-export of imported materials using stored VMT params from manifest
- Custom material files automatically packed into BSP via bspzip

### Fixed
- Compile pipeline failing on relative VMF paths (now converts to absolute)
- Spawn validation failing on imported maps

## [1.1.0] - 2026-02-12

### Added

#### Source → UE Import Pipeline (Phase 11)
- VMF file parser and importer (geometry reconstruction via CSG plane clipping)
- BSP import via bundled BSPSource decompiler (Java)
- Material import with VMT parsing, reverse tool mapping, and placeholder generation
- VTF texture reader for Source textures (DXT1/DXT3/DXT5/BGRA8/UV88)
- VPK archive reader for stock game materials (hl2, cstrike, platform)
- Masked and translucent material support (3-tier: opaque/masked/translucent)
- Tool texture visualization with semi-transparent colored materials
- Console commands: ImportVMF, ImportBSP

#### MDL Model Import (Phase 12)
- MDL/VVD/VTX binary reader for Source model files
- Model import as UStaticMesh on ASourceProp actors
- LOD, body group, and hitbox support
- PHY collision data reader
- Round-trip model export (import → re-export preserves data)
- Custom model packaging in full export pipeline

#### Brush Entity Parity (Phase 13)
- ASourceBrushEntity class for brush-based entities (func_detail, triggers, etc.)
- Full keyvalue, spawnflag, and I/O connection preservation on import
- FGD-driven property editing for all brush entity types
- Parent-child entity attachment (parentname support)
- Round-trip export for brush entities with original plane/UV data

#### Advanced Editor Tools (Phase 14)
- Source I/O Node Graph Editor with auto-layout and bidirectional selection sync
- Custom graph nodes with entity-colored title bars, FGD property widgets, and connection editing
- Pin coloring (output=green, input=blue) and wire coloring by entity type
- Wire tooltips showing connection details (output → target.input, delay, refire)
- Jump-to-node navigation (select entity → graph pans to its node)
- I/O connection parameter display and inline editing
- Entity-specific editor sprites (lights, spawns, props, triggers)
- PIE runtime with Source spawn points, first-person walking pawn, and noclip
- Tool texture per-face hiding in PIE mode
- SourceBridge GameMode for Play-In-Editor testing

### Fixed
- VMF plane winding for correct front-face rendering on import
- UV computation for imported brush textures
- Material rendering using Material Instance Dynamic instead of per-material UMaterial
- VPK archives being wiped during BSP import cache clear
- VMT parser handling unquoted keys/values and nested quoted strings (water materials)
- Material search ordering (check VPK before recursive disk scan)
- Large map import performance (progress bars, reduced logging)
- MDL header offset bug causing model imports to fail
- Source → UE rotation conversion for props
- FGD parser stack overflow from circular inheritance
- I/O graph showing wrong classnames during import (deferred rebuild)
- I/O connections from BSPSource-decompiled VMFs using ESC (0x1b) separator

### Changed
- Release workflow now downloads and bundles BSPSource v1.4.7 automatically

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
