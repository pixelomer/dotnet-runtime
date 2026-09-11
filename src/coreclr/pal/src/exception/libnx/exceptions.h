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
#define NXEX_STACK_SIZE 65536
#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>
#include <switch/arm/thread_context.h>
#include <libs/Common/libnx_threads.h>
typedef struct
{
    ThreadContext context;
    uint64_t far;
    uint32_t esr, description;
    uintptr_t stackLow, stackHigh, stackTop;
    uint32_t active;
    __attribute__((aligned(16))) unsigned char stack[NXEX_STACK_SIZE];
    LibnxThreadRegistration* registration;
} LibnxExceptionState;
#ifdef __cplusplus
extern "C" {
#endif
bool PAL_LibnxEnableExceptions(void);
void PAL_LibnxDisableExceptions(void);
void PAL_LibnxBeginException(LibnxExceptionState* state) __attribute__((noreturn));
void* PAL_LibnxExceptionEntryAddress(void);
#ifdef __cplusplus
}
#endif
#endif
