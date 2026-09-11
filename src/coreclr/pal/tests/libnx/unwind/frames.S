.text
.align 2
.global ProbeUnwindFrame
.type ProbeUnwindFrame, %function
ProbeUnwindFrame:
.cfi_startproc
sub sp, sp, #176
.cfi_def_cfa_offset 176
stp x19, x20, [sp, #0]
.cfi_offset 19, -176
.cfi_offset 20, -168
stp x21, x22, [sp, #16]
.cfi_offset 21, -160
.cfi_offset 22, -152
stp x23, x24, [sp, #32]
.cfi_offset 23, -144
.cfi_offset 24, -136
stp x25, x26, [sp, #48]
.cfi_offset 25, -128
.cfi_offset 26, -120
stp x27, x28, [sp, #64]
.cfi_offset 27, -112
.cfi_offset 28, -104
stp x29, x30, [sp, #80]
.cfi_offset 29, -96
.cfi_offset 30, -88
stp d8, d9, [sp, #96]
.cfi_offset 72, -80
.cfi_offset 73, -72
stp d10, d11, [sp, #112]
.cfi_offset 74, -64
.cfi_offset 75, -56
stp d12, d13, [sp, #128]
.cfi_offset 76, -48
.cfi_offset 77, -40
stp d14, d15, [sp, #144]
.cfi_offset 78, -32
.cfi_offset 79, -24
mov x29, sp
str x2, [sp, #160]
mov x19, #4115
mov x20, #4116
mov x21, #4117
mov x22, #4118
mov x23, #4119
mov x24, #4120
mov x25, #4121
mov x26, #4122
mov x27, #4123
mov x28, #4124
mov x3, #32776
fmov d8, x3
mov x3, #32777
fmov d9, x3
mov x3, #32778
fmov d10, x3
mov x3, #32779
fmov d11, x3
mov x3, #32780
fmov d12, x3
mov x3, #32781
fmov d13, x3
mov x3, #32782
fmov d14, x3
mov x3, #32783
fmov d15, x3
mov x3, x0
mov x0, x1
mov x1, x3
bl ProbeUnwindInner
ldr x2, [sp, #160]
str x19, [x2]
fmov x3, d8
str x3, [x2, #8]
ldp x19, x20, [sp, #0]
.cfi_restore 19
.cfi_restore 20
ldp x21, x22, [sp, #16]
.cfi_restore 21
.cfi_restore 22
ldp x23, x24, [sp, #32]
.cfi_restore 23
.cfi_restore 24
ldp x25, x26, [sp, #48]
.cfi_restore 25
.cfi_restore 26
ldp x27, x28, [sp, #64]
.cfi_restore 27
.cfi_restore 28
ldp x29, x30, [sp, #80]
.cfi_restore 29
.cfi_restore 30
ldp d8, d9, [sp, #96]
.cfi_restore 72
.cfi_restore 73
ldp d10, d11, [sp, #112]
.cfi_restore 74
.cfi_restore 75
ldp d12, d13, [sp, #128]
.cfi_restore 76
.cfi_restore 77
ldp d14, d15, [sp, #144]
.cfi_restore 78
.cfi_restore 79
add sp, sp, #176
.cfi_def_cfa_offset 0
ret
.cfi_endproc
.size ProbeUnwindFrame, .-ProbeUnwindFrame
.global ProbeUnwindInner
.type ProbeUnwindInner, %function
ProbeUnwindInner:
.cfi_startproc
sub sp, sp, #176
.cfi_def_cfa_offset 176
stp x19, x20, [sp, #0]
.cfi_offset 19, -176
.cfi_offset 20, -168
stp x21, x22, [sp, #16]
.cfi_offset 21, -160
.cfi_offset 22, -152
stp x23, x24, [sp, #32]
.cfi_offset 23, -144
.cfi_offset 24, -136
stp x25, x26, [sp, #48]
.cfi_offset 25, -128
.cfi_offset 26, -120
stp x27, x28, [sp, #64]
.cfi_offset 27, -112
.cfi_offset 28, -104
stp x29, x30, [sp, #80]
.cfi_offset 29, -96
.cfi_offset 30, -88
stp d8, d9, [sp, #96]
.cfi_offset 72, -80
.cfi_offset 73, -72
stp d10, d11, [sp, #112]
.cfi_offset 74, -64
.cfi_offset 75, -56
stp d12, d13, [sp, #128]
.cfi_offset 76, -48
.cfi_offset 77, -40
stp d14, d15, [sp, #144]
.cfi_offset 78, -32
.cfi_offset 79, -24
mov x29, sp
mov x16, x0
mov x19, #0x3333
mov x20, #0x3333
mov x21, #0x3333
mov x22, #0x3333
mov x23, #0x3333
mov x24, #0x3333
mov x25, #0x3333
mov x26, #0x3333
mov x27, #0x3333
mov x28, #0x3333
fmov d8, x19
fmov d9, x19
fmov d10, x19
fmov d11, x19
fmov d12, x19
fmov d13, x19
fmov d14, x19
fmov d15, x19
str x19, [x1, #160]
str x20, [x1, #168]
str x21, [x1, #176]
str x22, [x1, #184]
str x23, [x1, #192]
str x24, [x1, #200]
str x25, [x1, #208]
str x26, [x1, #216]
str x27, [x1, #224]
str x28, [x1, #232]
str x29, [x1, #0xf0]
str x30, [x1, #0xf8]
mov x3, sp
str x3, [x1, #0x100]
adr x3, .Lsnapshot
str x3, [x1, #0x108]
str d8, [x1, #400]
str d9, [x1, #416]
str d10, [x1, #432]
str d11, [x1, #448]
str d12, [x1, #464]
str d13, [x1, #480]
str d14, [x1, #496]
str d15, [x1, #512]
.Lsnapshot:
mov x0, x1
blr x16
ldp x19, x20, [sp, #0]
.cfi_restore 19
.cfi_restore 20
ldp x21, x22, [sp, #16]
.cfi_restore 21
.cfi_restore 22
ldp x23, x24, [sp, #32]
.cfi_restore 23
.cfi_restore 24
ldp x25, x26, [sp, #48]
.cfi_restore 25
.cfi_restore 26
ldp x27, x28, [sp, #64]
.cfi_restore 27
.cfi_restore 28
ldp x29, x30, [sp, #80]
.cfi_restore 29
.cfi_restore 30
ldp d8, d9, [sp, #96]
.cfi_restore 72
.cfi_restore 73
ldp d10, d11, [sp, #112]
.cfi_restore 74
.cfi_restore 75
ldp d12, d13, [sp, #128]
.cfi_restore 76
.cfi_restore 77
ldp d14, d15, [sp, #144]
.cfi_restore 78
.cfi_restore 79
add sp, sp, #176
.cfi_def_cfa_offset 0
ret
.cfi_endproc
.size ProbeUnwindInner, .-ProbeUnwindInner
.global ProbeUnwindLeaf
.type ProbeUnwindLeaf, %function
ProbeUnwindLeaf:
.cfi_startproc
.cfi_def_cfa sp, 0
.cfi_undefined 19
ret
.cfi_endproc
.size ProbeUnwindLeaf, .-ProbeUnwindLeaf
.section .note.GNU-stack,"",%progbits
