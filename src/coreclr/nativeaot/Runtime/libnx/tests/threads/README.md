# Registered-thread synchronization probe

Build the source runtime using the [PAL probe prerequisites](../pal/README.md),
then run `python3
src/coreclr/nativeaot/Runtime/libnx/tests/threads/build.py` from the repository
root. The test links actual runtime archive sections, without managed startup or
runtime stubs. It writes only `sdmc:/switch/dotnet-thread-registry-probe.txt` and
returns to hbmenu; preserve existing output there if needed. It requires
Mesosphere's thread activity/context APIs and access to CPU cores 0 and 1.

The test covers:

- Repeated pause/context/resume cycles, with a barrier while a thread is intentionally
  held paused. Its counter must remain stable until explicit resume.
- Store-buffer litmus rounds: the main thread invokes the process write
  barrier; both threads observing the other's flag as zero is forbidden.
- Concurrent workers performing registration cycles, each with duplicate PAL
  handles, while the main thread repeatedly invokes the barrier.

These checks do not initialize NativeAOT or validate managed root relocation,
exceptions, finalizers or thread-static lifetime. Read `../../THREAD_ORDERING.md`
for the kernel ordering argument and the registry's scope.
