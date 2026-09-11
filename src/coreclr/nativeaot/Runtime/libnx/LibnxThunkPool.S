
// Keep these dimensions in sync with ThunksMapping.cpp: 64 pages, 255
// 16-byte thunks/page. Each RW slot holds context and target; the final
// pointer in each page holds the common stub. Only x16/x17 are scratch.
// ADRP/ADD relocations are resolved at link time; code is never modified.
.section .bss.LibnxThunkData,"aw",%nobits
.balign 4096
.global LibnxThunkData
.hidden LibnxThunkData
.type LibnxThunkData,%object
LibnxThunkData:
    .skip 64 * 4096
.size LibnxThunkData, .-LibnxThunkData

.section .text.LibnxThunkCode,"ax",%progbits
.balign 4096
.global LibnxThunkCode
.hidden LibnxThunkCode
.type LibnxThunkCode,%function
LibnxThunkCode:
.set page, 0
.rept 64
    .set slot, 0
    .rept 255
        adrp x16, LibnxThunkData + page * 4096 + slot * 16
        add x16, x16, :lo12:(LibnxThunkData + page * 4096 + slot * 16)
        ldr x17, [x16, #(4088 - slot * 16)]
        br x17
        .set slot, slot + 1
    .endr
    .space 16
    .set page, page + 1
.endr
.size LibnxThunkCode, .-LibnxThunkCode
.section .note.GNU-stack,"",%progbits
