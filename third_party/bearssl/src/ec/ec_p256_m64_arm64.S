/*
 * Xisto patch: P-256 field Montgomery multiplication for AArch64.
 *   void xs_f256_montymul_arm64(uint64_t d[4], const uint64_t a[4], const uint64_t b[4])
 * d = a*b/2^256 mod p, result < 2^256 (same contract as f256_montymul in
 * ec_p256_m64.c; d may alias a or b). Interleaved CIOS: one row a[i]*b,
 * then one reduction step. Since p = -1 mod 2^64 the reduction factor is
 * the low limb itself, and m*p = m*2^256 - m*2^224 + m*2^192 + m*2^96 - m
 * needs only shifts and carries. No branches, no secret-dependent loads.
 */
#if defined(__aarch64__)
#if defined(__APPLE__)
#define FN _xs_f256_montymul_arm64
#else
#define FN xs_f256_montymul_arm64
#endif
	.text
	.p2align 4
	.globl FN
#if defined(__ELF__) /* (Mach-O and Windows' COFF have no .type) */
	.type FN, %function
#endif
FN:
	/* b in x3..x6 */
	ldp	x3, x4, [x2]
	ldp	x5, x6, [x2, #16]
	/* accumulator x7..x11 (x11 = top bit), temporaries x12..x17 */
	mov	x7, xzr
	mov	x8, xzr
	mov	x9, xzr
	mov	x10, xzr
	mov	x11, xzr
	mov	x2, #4			/* rows left */
1:
	ldr	x12, [x1], #8		/* a[i] */
	/* row = a[i] * b, 5 limbs: x13 x14 x15 x16 x17 */
	mul	x13, x12, x3
	mul	x14, x12, x4
	mul	x15, x12, x5
	mul	x16, x12, x6
	umulh	x17, x12, x3		/* mul/umulh leave the flags alone: the adcs chain holds */
	adds	x14, x14, x17
	umulh	x17, x12, x4
	adcs	x15, x15, x17
	umulh	x17, x12, x5
	adcs	x16, x16, x17
	umulh	x17, x12, x6
	adc	x17, x17, xzr
	/* acc += row; x12 = carry out (top of the 6th limb) */
	adds	x7, x7, x13
	adcs	x8, x8, x14
	adcs	x9, x9, x15
	adcs	x10, x10, x16
	adcs	x11, x11, x17
	adc	x12, xzr, xzr
	/* reduction with m = x7: add m*p, then shift right one limb */
	lsl	x13, x7, #32		/* m << 32 */
	lsr	x14, x7, #32		/* m >> 32 */
	subs	x15, x7, x13		/* low  of m*0xFFFFFFFF00000001 */
	sbc	x16, x7, x14		/* high of m*0xFFFFFFFF00000001 */
	adds	x7, x8, x13		/* limb 1 + (m << 32) -> new limb 0 */
	adcs	x8, x9, x14		/* limb 2 + (m >> 32) -> new limb 1 */
	adcs	x9, x10, x15		/* limb 3 + low      -> new limb 2 */
	adcs	x10, x11, x16		/* limb 4 + high     -> new limb 3 */
	adc	x11, x12, xzr		/* top */
	subs	x2, x2, #1
	b.ne	1b
	/* conditional subtraction of p = (2^64-1, 2^32-1, 0, 2^64-2^32+1) */
	mov	x12, #-1
	mov	x13, #0xFFFFFFFF
	mov	x14, #1
	movk	x14, #0xFFFF, lsl #32
	movk	x14, #0xFFFF, lsl #48
	subs	x15, x7, x12
	sbcs	x16, x8, x13
	sbcs	x17, x9, xzr
	sbcs	x12, x10, x14
	sbcs	xzr, x11, xzr		/* C set: no borrow, so acc >= p: take acc - p */
	csel	x7, x15, x7, cs
	csel	x8, x16, x8, cs
	csel	x9, x17, x9, cs
	csel	x10, x12, x10, cs
	stp	x7, x8, [x0]
	stp	x9, x10, [x0, #16]
	ret
#endif
