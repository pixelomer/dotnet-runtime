#include "LibnxExceptions.h"
.section .text.__libnx_exception_entry,"ax",%progbits
.global __libnx_exception_entry
.type __libnx_exception_entry,%function
__libnx_exception_entry:
    cbz x1, .LexceptionFatal
    mov w8, w0
    // GCC's -mtp=soft helper preserves every register except x0 and uses no
    // stack in the libnx 4.12 ABI. Kernel frame already preserves x0..x8/LR.
    bl __aarch64_read_tp
    add x0, x0, :tprel_hi12:tls_LibnxExceptionState
    add x0, x0, :tprel_lo12_nc:tls_LibnxExceptionState
    ldr x0, [x0]
    cbz x0, .LexceptionFatal
    ldr w2, [x0, #NXEX_ACTIVE]
    cbnz w2, .LexceptionFatal
    mov w2, #1
    str w2, [x0, #NXEX_ACTIVE]
    str w8, [x0, #NXEX_DESC]

    // Preserve registers absent from the kernel's small user-exception frame.
    str x9, [x0, #72]
    stp x10, x11, [x0, #80]
    stp x12, x13, [x0, #96]
    stp x14, x15, [x0, #112]
    stp x16, x17, [x0, #128]
    stp x18, x19, [x0, #144]
    stp x20, x21, [x0, #160]
    stp x22, x23, [x0, #176]
    stp x24, x25, [x0, #192]
    stp x26, x27, [x0, #208]
    stp x28, x29, [x0, #224]
    ldp x2, x3, [x1, #0]
    stp x2, x3, [x0, #0]
    ldp x2, x3, [x1, #16]
    stp x2, x3, [x0, #16]
    ldp x2, x3, [x1, #32]
    stp x2, x3, [x0, #32]
    ldp x2, x3, [x1, #48]
    stp x2, x3, [x0, #48]
    ldr x2, [x1, #64]
    str x2, [x0, #64]
    ldr x2, [x1, #72]
    str x2, [x0, #240]
    ldp x2, x3, [x1, #80]
    stp x2, x3, [x0, #248]
    ldr w2, [x1, #96]
    str w2, [x0, #264]
    ldr w2, [x1, #108]
    str w2, [x0, #NXEX_ESR]
    ldr x2, [x1, #112]
    str x2, [x0, #NXEX_FAR]
    stp q0, q1, [x0, #272]
    stp q2, q3, [x0, #304]
    stp q4, q5, [x0, #336]
    stp q6, q7, [x0, #368]
    stp q8, q9, [x0, #400]
    stp q10, q11, [x0, #432]
    stp q12, q13, [x0, #464]
    stp q14, q15, [x0, #496]
    stp q16, q17, [x0, #528]
    stp q18, q19, [x0, #560]
    stp q20, q21, [x0, #592]
    stp q22, q23, [x0, #624]
    stp q24, q25, [x0, #656]
    stp q26, q27, [x0, #688]
    stp q28, q29, [x0, #720]
    stp q30, q31, [x0, #752]
    mrs x2, fpcr
    str w2, [x0, #784]
    mrs x2, fpsr
    str w2, [x0, #788]
    mrs x2, tpidr_el0
    str x2, [x0, #792]

    // The sole kernel call in exception mode returns onto a private dispatcher
    // stack. All subsequent C/runtime work runs as ordinary user code.
    str x0, [x1, #0]
    ldr x2, [x0, #NXEX_STACK_TOP]
    str x2, [x1, #80]
    adrp x2, LibnxDispatchHardwareException
    add x2, x2, :lo12:LibnxDispatchHardwareException
    str x2, [x1, #88]
    str xzr, [x1, #72]
    mov x29, xzr
    mov w0, wzr
    svc #0x28
    b .
.LexceptionFatal:
    mov w0, #0xf801
    svc #0x28
    b .
.size __libnx_exception_entry,.-__libnx_exception_entry

.section .text.LibnxRestoreHardwareThrow,"ax",%progbits
.global LibnxRestoreHardwareThrow
.type LibnxRestoreHardwareThrow,%function
LibnxRestoreHardwareThrow:
    mov x16, x0
    ldr x17, [x16, #256]
    ldr w9, [x16, #784]
    msr fpcr, x9
    ldr w9, [x16, #788]
    msr fpsr, x9
    ldr x9, [x16, #792]
    msr tpidr_el0, x9
    ldp q0, q1, [x16, #272]
    ldp q2, q3, [x16, #304]
    ldp q4, q5, [x16, #336]
    ldp q6, q7, [x16, #368]
    ldp q8, q9, [x16, #400]
    ldp q10, q11, [x16, #432]
    ldp q12, q13, [x16, #464]
    ldp q14, q15, [x16, #496]
    ldp q16, q17, [x16, #528]
    ldp q18, q19, [x16, #560]
    ldp q20, q21, [x16, #592]
    ldp q22, q23, [x16, #624]
    ldp q24, q25, [x16, #656]
    ldp q26, q27, [x16, #688]
    ldp q28, q29, [x16, #720]
    ldp q30, q31, [x16, #752]
    ldr x9, [x16, #248]
    mov sp, x9
    ldr w9, [x16, #264]
    msr nzcv, x9
    ldp x0, x1, [x16, #0]
    ldp x2, x3, [x16, #16]
    ldp x4, x5, [x16, #32]
    ldp x6, x7, [x16, #48]
    ldp x8, x9, [x16, #64]
    ldp x10, x11, [x16, #80]
    ldp x12, x13, [x16, #96]
    ldp x14, x15, [x16, #112]
    ldp x18, x19, [x16, #144]
    ldp x20, x21, [x16, #160]
    ldp x22, x23, [x16, #176]
    ldp x24, x25, [x16, #192]
    ldp x26, x27, [x16, #208]
    ldp x28, x29, [x16, #224]
    ldr x30, [x16, #240]
    br x17
.size LibnxRestoreHardwareThrow,.-LibnxRestoreHardwareThrow
.section .note.GNU-stack,"",%progbits
