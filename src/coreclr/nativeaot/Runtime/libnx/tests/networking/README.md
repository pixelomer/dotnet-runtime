# Horizon synchronous IPv4 socket probe

Build the source Horizon native runtime/BCL and managed SDK using the
[managed library guide](../../MANAGED_LIBRARIES.md) and its linked prerequisites.
Export `ICU_NX_INSTALL_DIR`, then run from the runtime root:

```sh
python3 src/coreclr/nativeaot/Runtime/libnx/tests/networking/build.py
```

The build uses ILC 9.0.3, SDK 10.0.111 and `--noinlinetls`. It rejects IL
warnings and Linux TLS accesses/relocations. Outputs are under
`artifacts/libnx-networking-test/`.

The workload exercises IPv4 loopback TCP connect/listen/accept, endpoint checks,
TCP_NODELAY, bidirectional transfer and half-close/EOF, plus UDP datagrams and
source endpoints. Stream transfers handle partial progress and sockets have
send/receive timeouts. Handles use scoped disposal and collections run between
rounds. Only loopback addresses and ephemeral ports are used; no external
connection, credentials or game data are required.

The launcher initializes libnx sockets, writes
`sdmc:/switch/nativeaot-networking-test.txt` and requests application exit to
HOME. Preserve an existing log before use and use full application memory.

Async sockets, DNS, IPv6, TLS, HTTP, external connectivity and concurrent socket
operations are outside this probe. The native event-port fallback returns
ENOSYS when epoll/kqueue are unavailable; replacing only the NativeAOT SDK
assemblies does not select a platform-specific System.Net.Sockets implementation.

SafeSocketHandle's field initializer reaches SocketAsyncEngine during socket
construction, including synchronous use. The source tree's poll-based
`SocketAsyncEngine.Libnx.cs` is selected by building System.Net.Sockets for
libnx; substituting only CoreLib cannot change that engine selection.
