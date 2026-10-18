/* ARM RTABI32 signed quotient r0:r1, remainder r2:r3; 8-byte aligned stack. */
.syntax unified
.arm
.text
.align 2
.global __aeabi_ldivmod
.hidden __aeabi_ldivmod
.hidden cns2_i64_divmod
.type __aeabi_ldivmod, %function
__aeabi_ldivmod:
    push {r4, lr}
    sub sp, sp, #16
    add r4, sp, #8
    str r4, [sp, #0]
    bl cns2_i64_divmod
    ldr r2, [sp, #8]
    ldr r3, [sp, #12]
    add sp, sp, #16
    pop {r4, pc}
.size __aeabi_ldivmod, .-__aeabi_ldivmod
.section .note.GNU-stack,"",%progbits
