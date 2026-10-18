/* ARM RTABI32: unsigned quotient in r0:r1, remainder in r2:r3.
 * Preserve 8-byte stack alignment and the caller's r4 and return address.
 * The C worker takes numerator in r0:r1, denominator in r2:r3, and its
 * fifth argument (remainder pointer) at the caller's stack offset zero. */
.syntax unified
.arm
.text
.align 2
.global __aeabi_uldivmod
.hidden __aeabi_uldivmod
.hidden cns2_u64_divmod
.type __aeabi_uldivmod, %function
__aeabi_uldivmod:
    push {r4, lr}
    sub sp, sp, #16
    add r4, sp, #8
    str r4, [sp, #0]
    bl cns2_u64_divmod
    ldr r2, [sp, #8]
    ldr r3, [sp, #12]
    add sp, sp, #16
    pop {r4, pc}
.size __aeabi_uldivmod, .-__aeabi_uldivmod
.section .note.GNU-stack,"",%progbits
