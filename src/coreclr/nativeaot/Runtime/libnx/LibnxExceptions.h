#pragma once
#define NXEX_CONTEXT_SIZE 800
#define NXEX_FAR 800
#define NXEX_ESR 808
#define NXEX_DESC 812
#define NXEX_STACK_LOW 816
#define NXEX_STACK_HIGH 824
#define NXEX_STACK_TOP 832
#define NXEX_ACTIVE 840
#define NXEX_STACK 848
#define NXEX_STACK_SIZE 32768
#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>
#include <switch/arm/thread_context.h>
typedef struct
{
    ThreadContext context;
    uint64_t far;
    uint32_t esr, description;
    uintptr_t stackLow, stackHigh, stackTop;
    uint32_t active;
    __attribute__((aligned(16))) unsigned char stack[NXEX_STACK_SIZE];
} LibnxExceptionState;
#ifdef __cplusplus
extern "C" {
#endif
bool LibnxInitializeExceptionState(void);
void LibnxDestroyExceptionState(void);
void LibnxDispatchHardwareException(LibnxExceptionState* state) __attribute__((noreturn));
void LibnxRestoreHardwareThrow(ThreadContext* context) __attribute__((noreturn));
// Optional diagnostic observer, called after leaving kernel exception mode.
// It must not allocate, take locks, enter managed code, mutate the context, or
// retain the context pointer. Copy only into preallocated native storage.
void LibnxRuntimeHardwareFault(const ThreadContext* context, uint64_t far, uint32_t esr) __attribute__((weak));
#ifdef __cplusplus
}
#endif
#endif
