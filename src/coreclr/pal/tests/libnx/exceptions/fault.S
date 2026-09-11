.text
.global ProbeFault, ProbeFaultInstruction, ProbeFaultContinuation
.type ProbeFault,%function
ProbeFault:
.cfi_startproc
    stp x29, x30, [sp, #-32]!
.cfi_def_cfa_offset 32
.cfi_offset x29, -32
.cfi_offset x30, -24
    str x19, [sp, #16]
.cfi_offset x19, -16
    mov x29, sp
    mov x19, x0
    mov x16, #0x1616
    mov x17, #0x1717
    mov x9, #0x3030
    fmov d0, x9
    mov x9, #0x3131
    fmov d31, x9
    mov x0, xzr
ProbeFaultInstruction:
    ldr x0, [x0]
ProbeFaultContinuation:
    stp x0, x16, [x19]
    str x17, [x19, #16]
    str d0, [x19, #24]
    str d31, [x19, #32]
    ldr x19, [sp, #16]
    ldp x29, x30, [sp], #32
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size ProbeFault,.-ProbeFault
.section .note.GNU-stack,"",%progbits
