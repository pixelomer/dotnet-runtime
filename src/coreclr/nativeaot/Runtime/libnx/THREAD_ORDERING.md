# Experimental Horizon thread suspension and ordering

This implementation relies on the Mesosphere scheduler contract described
below. It is not a portable ordering contract for every Horizon kernel.

## Ordering argument and primary sources

The source references below use Atmosphere **1.11.2**. The Mesosphere metadata
query alone does not identify an exact source revision; kernel compatibility
requires the scheduler behavior described here.

The rendezvous publishes the caller's writes, pauses each registered thread,
waits for the kernel to acknowledge it is no longer executing, and resumes it.
A local DMB alone or broadcast page-table invalidation does not implement this.

- ARM64 `ScheduleImpl` executes DMB ISH and saves the old thread's context before
  invoking `SwitchThread`. Context completion is published with a release store.
  [scheduler assembly](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/mesosphere/kernel/source/arch/arm64/kern_k_scheduler_asm.s)
- `SwitchThread` publishes the new `m_current_thread`. This is `util::Atomic`,
  with sequentially consistent default loads/stores. On ARM64 those include
  acquire/release ordering, so observing the switch-away orders preceding user
  writes as well as the saved context.
  [scheduler](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libmesosphere/source/kern_k_scheduler.cpp),
  [atomic field](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libmesosphere/include/mesosphere/kern_k_scheduler.hpp),
  [atomic implementation](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libvapours/include/vapours/util/arch/arm64/util_atomic.hpp)
- `SetActivity(Paused)` requests suspension under the scheduler lock and waits
  until the target is unpinned and no core reports it current. Context capture
  follows this acknowledgment. Resume updates scheduling under the same lock;
  release/acquire scheduling and the scheduler's barriers order the caller's
  preceding writes before resumed user execution.
  [thread activity/context](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libmesosphere/source/kern_k_thread.cpp),
  [scheduler lock](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libmesosphere/include/mesosphere/kern_k_scheduler_lock.hpp)

Already GC-paused threads remain paused during later barriers. Their previous
writes were ordered by their original pause. GC updates are published before
these threads finally resume. The current thread executes an explicit full
fence. New registrations synchronize on the same registry lock before managed
execution; departing threads unregister before their real handle is closed.

## Lifecycle and lock rules

The registry is independent of ThreadStore/GC locks. It owns refcounted records,
not kernel handles; duplicate PAL wrappers share one record. Every thread that
can execute managed code, and each runtime GC worker, must register. Unattached
native library workers are outside this registry: they must not manipulate
managed roots/write-watch state, and must explicitly register if they need this
rendezvous contract. This is not a general enumeration of arbitrary process
threads. Native API integration must preserve that boundary.

While holding the registry lock, pause/resume/barrier code uses only its own
records, inline memory operations and kernel calls. It does not allocate, print,
or acquire ThreadStore/GC locks. Allocation/free happens outside the lock.
Current-thread teardown removes the record before libnx releases its handle.
Kernel errors are fatal rather than reported as a successful barrier. Arbitrary
external suspension/forced termination of registered threads is unsupported.

## GC integration

Thread construction preallocates a native context. `Thread::Hijack` uses a
synchronous pause on Horizon. A stopped location must be managed, safe to scan,
unwindable, and outside DoNotTriggerGC. Other locations resume for a later retry.
The thread remains paused through stack/root scanning and relocation. Its cached
frame uses the existing INTERRUPTED_THREAD_MARKER context path.

Horizon provides a read-only thread context, so GC cannot write relocated object
pointers back into CPU registers. Precise root callbacks therefore add PINNED
only when the reported slot lies in the copied register context. Real stack/root
slots retain normal moving-GC behavior. This is temporary collection pinning,
not conservative reporting of the whole stack or permanent GCHandles.

Resume happens after cached-frame reset, barriers, trap clearing and GC-event
publication. No unsupported signal callback is registered. The native registry
probe does not exercise managed exceptions, thread abort/redirection, root
relocation, finalization or background/server GC.

## Native probe

`tests/threads/` links the runtime archive sections to exercise held-thread
pause/resume, store-buffer ordering and concurrent registration lifetimes.
These checks complement the ordering argument; they do not establish managed
GC correctness.

The [managed stress workload](tests/managed/README.md) exercises compacting
collections with call-free managed loops, concurrent exceptions, TLS and
finalizers. It does not broaden the ordering contract to other kernels.
