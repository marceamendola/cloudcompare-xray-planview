# qXRayPlanView

CloudCompare standard plugin for point-cloud visualization workflows focused on X-Ray plan views, normal-direction overlays, and live Z slicing.

## Features

- X-Ray Overlay
  - Viewport-resolution overlay that hides the original RGB cloud while active.
  - Monochrome and height spectral color modes.
  - Global controls for all active X-Ray clouds.
  - Gamma control up to 10.
  - Background inversion for monochrome and spectral modes.

- Normals Overlay
  - Normal-based RGB visualization.
  - Palette selector: RGB, Soft RGB, BIM, and Warm / Cool.
  - View-filtered mode with adjustable filter strength.
  - Background gray control and gamma control.
  - Global controls for all active normals overlays.

- Z Slice Controls
  - Live filtering while moving sliders.
  - Center / Thickness mode.
  - Z min / Z max mode.
  - Works with multiple selected clouds.
  - Updates active X-Ray and Normals overlays when they are present.

- Restore Original Colors
  - Removes X-Ray and Normals overlays.
  - Clears Z visibility filtering.
  - Restores the viewport background to white.
  - Forces affected clouds back to visible and enabled.

## CloudCompare Integration

Copy this folder into:

```text
CloudCompare/plugins/core/Standard/qXRayPlanView
```

Then enable the plugin from `plugins/core/Standard/CMakeLists.txt`:

```cmake
option( PLUGIN_STANDARD_QXRAYPLANVIEW "Install qXRayPlanView plugin" OFF )

if ( PLUGIN_STANDARD_QXRAYPLANVIEW )
	add_subdirectory( qXRayPlanView )
endif()
```

Configure CloudCompare with:

```powershell
cmake -S . -B build -DPLUGIN_STANDARD_QXRAYPLANVIEW=ON
```

Build the plugin target:

```powershell
cmake --build build --config Release --target QXRAYPLANVIEW_PLUGIN
```

The resulting DLL must be copied to CloudCompare's `plugins` folder.

## Current Portable Build

In this workspace the plugin is built with:

```powershell
cmake --build $env:TEMP\ccxrbuild --config Release --target QXRAYPLANVIEW_PLUGIN
```

The output DLL is:

```text
%TEMP%\ccxrbuild\plugins\core\Standard\qXRayPlanView\Release\QXRAYPLANVIEW_PLUGIN.dll
```

Portable CloudCompare plugin destination:

```text
codex\CloudCompare-Portable-XRay\plugins\QXRAYPLANVIEW_PLUGIN.dll
```

## Usage Notes

- Activate overlays by selecting one or more point clouds first.
- X-Ray and Normals controls are single-instance panels.
- Controls are global: changing a control updates every active overlay of that type.
- Z Slice is a viewport child panel, fixed near the lower-left corner, so the viewport remains usable while the panel is open.
- Use the `x` button on each control panel to close it.
- Use `Restore Original Colors` as the reset action after X-Ray, Normals, or Z Slice work.

## Implementation Notes

- X-Ray and Normals overlays are separate `ccHObject` display layers.
- Original clouds are hidden while their overlay is active, then restored by the overlay toggle or reset action.
- Z slicing uses per-cloud visibility arrays when no overlay is active.
- When overlays are active, Z slicing updates the overlay layer's Z range instead of recoloring the source cloud.
- Normals are computed on demand using grid normals when available, with octree-based fallback.

## Validation

Last validated locally with:

```powershell
cmake --build $env:TEMP\ccxrbuild --config Release --target QXRAYPLANVIEW_PLUGIN
```

## Release Builds

This plugin must be built separately for each CloudCompare target. See [BUILD_MATRIX.md](BUILD_MATRIX.md) for the stable and beta/dev build matrix.
