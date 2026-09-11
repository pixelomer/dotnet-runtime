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
// Reuse an existing healthy pool, or initialize the requested default size.
bool nxvm_ensure_initialized(size_t backing_bytes);
bool nxvm_destroy(void); // Requires no live reservations; never frees mapped pages.
void *nxvm_reserve(size_t bytes, size_t alignment);
bool nxvm_commit(void *address, size_t bytes); // Idempotent; fresh pages are zero.
bool nxvm_decommit(void *address, size_t bytes); // Idempotent; backing is reusable.
bool nxvm_release(void *address, size_t bytes);
NxvmStats nxvm_stats(void);
