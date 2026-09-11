# Horizon synchronous IPv4 socket probe

Build the native runtime/BCL and managed SDK using the
[managed library guide](../../MANAGED_LIBRARIES.md) and its linked prerequisites.
The socket probe also requires a source-built Horizon System.Net.Sockets
assembly. Replacing only the five NativeAOT SDK assemblies is insufficient:
SafeSocketHandle initializes SocketAsyncEngine even for synchronous sockets.
The official Unix assembly requires event ports unavailable on Horizon.

## Build the socket assembly

From the runtime root, build the required reference projects and socket library.
The following builds an isolated reference pack from source:

```bash
(
set -e
socket_ref_pack="$PWD/artifacts/horizon-socket-refpack"
mkdir -p "$socket_ref_pack/ref/net9.0"
socket_build_options=(
  -c Release -p:TargetOS=libnx -p:TargetArchitecture=arm64
  -p:RuntimeFlavor=CoreCLR -p:UseNativeAotCoreLib=true -p:PublicSign=true
  -p:NativeAotSupported=true -p:EnableTrimAnalyzer=false
  "-p:MicrosoftNetCoreAppRefPackDir=$socket_ref_pack/"
  "-p:MicrosoftNetCoreAppRefPackRefDir=$socket_ref_pack/ref/net9.0/"
)
for socket_reference in Microsoft.Win32.Primitives System.Collections \
  System.Collections.Concurrent System.Diagnostics.DiagnosticSource \
  System.Diagnostics.Tracing System.Memory System.Net.NameResolution \
  System.Net.Primitives System.Runtime System.Runtime.InteropServices \
  System.Threading System.Threading.Overlapped System.Threading.ThreadPool \
  System.Threading.Thread; do
  ./dotnet.sh build "src/libraries/$socket_reference/ref/$socket_reference.csproj" \
    "${socket_build_options[@]}" -t:Rebuild
done
./dotnet.sh build src/libraries/System.Net.Sockets/src/System.Net.Sockets.csproj \
  "${socket_build_options[@]}" -p:TargetFramework=net9.0-libnx -t:Rebuild
)
```

The output is
`artifacts/bin/System.Net.Sockets/Release/net9.0-libnx/System.Net.Sockets.dll`.
With `ICU_NX_INSTALL_DIR` exported, build the probe from the runtime root:

```sh
python3 src/coreclr/nativeaot/Runtime/libnx/tests/networking/build.py
```

The build stages the source SDK, replaces the ILC socket reference and checks
that it is selected exclusively. `--unix-sockets-control` selects the official
Unix assembly to exercise its unsupported event-port path. The build uses
ILC 9.0.3, SDK 10.0.111 and `--noinlinetls`; it rejects IL warnings and Linux
TLS accesses. Outputs are under `artifacts/libnx-networking-test/`.

## Scope

The workload exercises loopback TCP connect/listen/accept, endpoint checks,
TCP_NODELAY, bidirectional transfer and half-close/EOF, plus UDP datagrams and
source endpoints. Stream transfers handle partial progress and use socket
timeouts. Handles use scoped disposal and collections run between rounds.
Only loopback addresses and ephemeral ports are used.

The launcher writes `sdmc:/switch/nativeaot-networking-test.txt` and requests
application exit to HOME. Preserve an existing log and use full application memory.

The Horizon poll engine uses native SocketEvent-sized buffers, clears reused
entries and reads the registration cookie with Volatile.Read. Diagnostic
formatting is excluded from Release builds. This probe does not cover arbitrary
close/reuse races, async operations, DNS, IPv6, TLS, HTTP or external connectivity.

## Native initialization

The launcher loads `icudt77l.dat` from RomFS, calls `udata_setCommonData`
before managed entry and retains the data until process exit. ICU archives
alone do not provide this data; formatting a socket error can initialize
globalization too. The native build defines `U_DISABLE_RENAMING=1` for the
unversioned ICU symbols and includes NACP alongside RomFS. It checks the NRO
asset header and RomFS bounds after conversion.

The launcher captures native stdout and stderr alongside its managed progress
log. BSD services remain initialized until process exit because the polling
engine owns a background thread. The native socket layer skips unsupported
close-on-exec operations on Horizon; it does not provide exec inheritance.

Connect and accept are blocking; the send/receive timeouts do not bound them.

## Socket buffer capacity and native control

The managed launcher sets `sb_efficiency=8` for its socket workload.
libnx multiplies the configured socket-buffer sum by this value when allocating
BSD transfer memory.

The [native control](native-control/) exercises matching TCP/UDP transfers,
half-close and socket close order without a managed runtime. From this directory:

```sh
SOCKET_EFFICIENCY=4 bash native-control/build.sh
SOCKET_EFFICIENCY=8 bash native-control/build.sh
```

It uses the devkitPro toolchain and libnx installation selected by `DEVKITPRO`.
Outputs are under `artifacts/libnx-native-networking-control/` at the runtime
root. Each build uses the same output names; preserve any outputs you need
before rebuilding. The control writes `sdmc:/switch/native-networking-control.txt`.
A failure returns immediately and relies on process teardown for remaining open
sockets; this is not a recovery-after-exhaustion test. As in the managed probe,
connect and accept are blocking despite the send/receive timeouts.
