# Build Matrix

This plugin is a native CloudCompare C++ plugin. A single DLL is not expected to work across every CloudCompare version because it depends on the CloudCompare plugin API, Qt major version, compiler ABI, and CPU architecture.

## Supported Release Targets

| Target | CloudCompare base | Qt | Compiler | Architecture | Status |
| --- | --- | --- | --- | --- | --- |
| Stable | v2.13.2 | Qt 5.x | MSVC x64 | x64 | Requires Qt5 build environment |
| Beta/dev | c7d5bb7 / v2.14 beta-dev | Qt 6.8.3 | MSVC 2022 | x64 | Builds locally |

## Current Local Beta Build

The current local portable CloudCompare build is based on a recent development commit:

```text
c7d5bb7 2026-07-14 Syntax update
```

It uses Qt 6.8.3 and builds with:

```powershell
cmake --build $env:TEMP\ccxrbuild --config Release --target QXRAYPLANVIEW_PLUGIN
```

## Stable Build Notes

CloudCompare v2.13.2 expects Qt5 during CMake configuration. A Qt6-only environment is not enough for this target.

A stable release artifact should be built from a clean CloudCompare v2.13.2 checkout with a matching Qt5 MSVC x64 development environment.

Expected configure shape:

```powershell
cmake -S CloudCompare-v2.13.2 -B build-cc-2.13.2 -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\5.15.x\msvc2019_64"
cmake --build build-cc-2.13.2 --config Release --target QXRAYPLANVIEW_PLUGIN
```

The exact Qt5 package should match the CloudCompare v2.13.2 Windows build used by the user.

## Installer Strategy

Create one installer per target. Each installer should:

- ask for or detect the CloudCompare installation folder;
- verify `CloudCompare.exe` exists;
- verify the intended compatibility target;
- copy `QXRAYPLANVIEW_PLUGIN.dll` into `CloudCompare\plugins`;
- back up an existing plugin DLL before overwrite;
- provide an uninstall action that removes only this plugin DLL.

Recommended release names:

```text
qXRayPlanView-CloudCompare-2.13.2-Windows-x64
qXRayPlanView-CloudCompare-2.14-beta-c7d5bb7-Windows-x64
```

