# Reverse P/Invoke GC entry transitions

Horizon reverse P/Invoke entry performs the native-to-cooperative transition
and checks for suspension. A managed GC poll must not run before that
transition: it would execute managed code while the thread is still preemptive,
and first-use compilation of PollGC can recursively enter a managed compiler
callback.

The JIT therefore omits the additional entry poll for reverse P/Invoke methods.
Their backward-edge polls remain intact. This does not disable GC suspension
or remove polling from callback loops.

The PAL defaults runtime-created threads to 1536 KiB when no configured default
is set. Explicit caller stack sizes and Thread_DefaultStackSize remain
available. This is a CoreCLR default, not a global change to libnx pthreads;
account for the per-thread committed memory when choosing a thread budget.
A larger stack does not make a managed poll before the GC transition valid.
