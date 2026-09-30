# SCN_01 · Rain Mountain Path

First-scene modular art prototype, modeled in Blender 4.3.2 from the approved illustrated reference. This is an actual editable 3D scene, not a concept image or game-ready certification.

## Deliverables
- `source/SCN_01_Rain_Mountain_Path.blend`: complete editable scene, semantic collections, material nodes, lights and overview camera
- `exports/SCN_01_Assembled.glb`: portable assembled preview without collision proxies, cameras or lights
- `exports/fbx/`: ten independently exported architectural/prop groups
- `previews/`: overview and shelter-detail renders
- `source/build_SCN_01.py`: reproducible geometry source, supplementary to the actual model
- `docs/module_manifest.json`: module inventory and limitations

## Scope
Wet stone path, roofed timber rest stop with overlapping slate tiles, sheltered fire/brazier, broken-wheel cart with wheel geometry and loose wheel, pushable barrel visual with separate contained-water surface, lower bypass trail, parapets, wooden fences, lanterns, vegetation, cliff rocks and distant south gate.

The reference's illustrative complexity is interpreted as a stylized low-poly prototype; final texture art and engine-grade surfacing are not complete. Geometry remains separated rather than collapsed into a single scene mesh. Cart wheels, cart boards, barrel staves, water, flame geometry and brazier are editable distinct mesh objects. Pushable means intended use, not implemented physics.

## Import and scale
Blender geometry is authored in meters, metric scale 1.0. FBX includes unit-scale metadata; UE uses centimeters. Enable the importer's scene/unit conversion and verify that the approximately 1.3 m barrel is approximately 130 cm tall before integrating. Do not apply an additional 100x scale without measuring. Scene-space placement is preserved in individual exports; set production pivots per gameplay requirements. Use the separated objects/collections to make engine modules; joining an entire export on import can destroy intended interaction boundaries.

UCX-named proxy boxes are included for seven groups as provisional collision references. They have not been validated by UE and do not certify that UE will associate multi-object group proxies automatically. Shelter currently has a foundation proxy only; posts/roof need dedicated production collision. Gate/backdrop collision is absent. Set actual convex hulls, naming, pivots, object mobility, mass, wetness and material rules in-engine, and test them.

## Materials and presentation
Blender procedural weathering and bump are editable and used in the renders. FBX/GLB preserve base material values, not the full procedural node graphs. Exported material appearance will therefore differ until textures are baked or UE materials recreated. Flame and lantern meshes use emission; light energy is scene presentation only. Water is a separate surface mesh, not fluid simulation. Rain-soaked appearance is represented by sheen, puddles and moss; no runtime weather system is included.

## Not yet verified
Blender acceptance checks passed: source reopened successfully; all ten FBX files and assembled GLB reimported successfully. Measured barrel group bounds are 1.236 × 1.236 × 1.4 m including its proxy. The separate water surface is 0.86 × 0.86 × 0.02 m. The assembled GLB retains 1,796 mesh objects and two assembly roots. Detailed counts are in `geometry_check.json` and `export_reimport_check.json`.

No UE import, engine material/physics/reaction hookup, navmesh, game integration, LODs, packed/baked texture set, lightmap UV certification, collision certification, frame-time budget or shipping acceptance has been performed. This package covers SCN_01 only and does not mean all requested scenes or characters are complete.
