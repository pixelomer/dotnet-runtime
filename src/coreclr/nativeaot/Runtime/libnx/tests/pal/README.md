# Source-built PAL component probe

Use Linux x86-64, the SDK pinned in `global.json`, devkitA64 and libnx.
Set `DEVKITPRO` to the devkitPro installation and `ICU_NX_INSTALL_DIR` to
the install directory produced by the
[Horizon ICU source recipe](https://github.com/pixelomer/mono-nx/blob/main/icu/build_icu.sh).
Build the runtime archive from the repository root, then link the probe:

```sh
ROOTFS_DIR="$DEVKITPRO" ./build.sh -s clr.nativeaotruntime -c Release \
  --cross -a arm64 --os libnx /p:NativeAotSupported=true \
  /p:EnableTrimAnalyzer=false -cmakeargs '-DFEATURE_EVENT_TRACE=OFF'
```

```sh
python3 src/coreclr/nativeaot/Runtime/libnx/tests/pal/build.py
```

The probe links selected functions from the actual workstation GC archive,
using section garbage collection. It does not call PalInit, enter managed code,
or provide fake replacements for unresolved runtime hooks. Outputs are under
`artifacts/libnx-pal-test/`. The probe writes
`sdmc:/switch/dotnet-pal-probe.txt` and returns to hbmenu on success.
Save existing files at that path before reuse if needed.

The probe exercises backing reuse/exhaustion, permission behavior, event
auto/manual reset and timeout, pthread stack/borrowed-handle checks, main stack
bounds and image-base lookup. It does not exercise managed GC, notification
teardown, exception handling or unwind walks. The implementation uses the
ordinary permission SVC for mutable module data and the relocated `_start`
symbol for image-base lookup.
