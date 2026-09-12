#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Experimental data-only virtual memory. Page-aligned lengths/addresses only.
// This is not mmap: release requires a complete reservation, and executable
// permissions, shared mappings and arbitrary fixed addresses are unsupported.
typedef struct {
    size_t capacity, committed, reserved, reservations;
    uint32_t last_svc_error;
    bool poisoned;
} NxvmStats;

bool nxvm_init(size_t backing_bytes);
// Opt-in data alias backend. Virtual capacity equals backing_bytes (including
// guard pages), rather than allowing sparse reservations larger than the pool.
// The backing stays allocated for the pool lifetime; page commitment changes
// permissions and zeroes fresh pages. Adjacent commits can coalesce in Horizon.
// Requires an own-process handle and process-code mapping SVCs; no fallback.
bool nxvm_init_protected(size_t backing_bytes);
// Reuse an existing healthy pool, or initialize the requested default size.
bool nxvm_ensure_initialized(size_t backing_bytes);
// Actual dedicated Horizon virtual arena, separate from physical commitment.
size_t nxvm_virtual_capacity(void);
uintptr_t nxvm_virtual_max_address(void);
bool nxvm_destroy(void); // Requires no live reservations; never frees mapped pages.
void *nxvm_reserve(size_t bytes, size_t alignment);
bool nxvm_commit(void *address, size_t bytes); // Idempotent; fresh pages are zero.
bool nxvm_decommit(void *address, size_t bytes); // Idempotent; backing is reusable.
bool nxvm_release(void *address, size_t bytes);
// Validate an owned, fully committed range without changing its contents.
bool nxvm_is_committed(void *address, size_t bytes);
NxvmStats nxvm_stats(void);
