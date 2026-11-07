/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "inner.h"

#define I31_LEN     ((BR_MAX_EC_SIZE + 61) / 31)
#define POINT_LEN   (1 + (((BR_MAX_EC_SIZE + 7) >> 3) << 1))
#define ORDER_LEN   ((BR_MAX_EC_SIZE + 7) >> 3)

/* see bearssl_ec.h */
#if BR_INT128
/*
 * Xisto patch: the tail of a P-256 signature, s = k^-1 * (m + x*r) mod n,
 * with 4x64-bit Montgomery arithmetic modulo the curve order n instead of
 * 31-bit limbs. The inversion k^-1 = k^(n-2) dominated the signature
 * (about 35 of 80 us). Constant-time: the only branches and table indexes
 * depend on the public exponent n-2; secret values only go through
 * multiplications, additions and masked selections.
 */
typedef unsigned __int128 xs_u128;

static const uint64_t XS_N[4] = {
	0xF3B9CAC2FC632551, 0xBCE6FAADA7179E84,
	0xFFFFFFFFFFFFFFFF, 0xFFFFFFFF00000000
};

/* d = a - b; returns the borrow (0 or 1) */
static uint64_t
xs_sub4(uint64_t *d, const uint64_t *a, const uint64_t *b)
{
	xs_u128 w;
	uint64_t borrow = 0;
	int i;

	for (i = 0; i < 4; i ++) {
		w = (xs_u128)a[i] - b[i] - borrow;
		d[i] = (uint64_t)w;
		borrow = (uint64_t)(w >> 127);
	}
	return borrow;
}

/* d = a mod n for a < 2n given with an extra top bit hi (0 or 1) */
static void
xs_reduce_once(uint64_t *d, const uint64_t *a, uint64_t hi)
{
	uint64_t t[4], borrow, keep, m;
	int i;

	borrow = xs_sub4(t, a, XS_N);
	/* keep a when it was already < n (borrow and no top bit) */
	keep = borrow & (hi ^ 1);
	m = -keep;
	for (i = 0; i < 4; i ++) {
		d[i] = (a[i] & m) | (t[i] & ~m);
	}
}

/* d = a + b mod n, for a, b < n */
static void
xs_addmod(uint64_t *d, const uint64_t *a, const uint64_t *b)
{
	uint64_t t[4], carry = 0;
	xs_u128 w;
	int i;

	for (i = 0; i < 4; i ++) {
		w = (xs_u128)a[i] + b[i] + carry;
		t[i] = (uint64_t)w;
		carry = (uint64_t)(w >> 64);
	}
	xs_reduce_once(d, t, carry);
}

/* d = a * b / 2^256 mod n (Montgomery, CIOS), for a, b < n */
static void
xs_montmul(uint64_t *d, const uint64_t *a, const uint64_t *b, uint64_t n0i)
{
	uint64_t t[6] = { 0, 0, 0, 0, 0, 0 };
	int i, j;

	for (i = 0; i < 4; i ++) {
		xs_u128 w;
		uint64_t c = 0, m;

		for (j = 0; j < 4; j ++) {
			w = (xs_u128)a[j] * b[i] + t[j] + c;
			t[j] = (uint64_t)w;
			c = (uint64_t)(w >> 64);
		}
		w = (xs_u128)t[4] + c;
		t[4] = (uint64_t)w;
		t[5] = (uint64_t)(w >> 64);

		m = t[0] * n0i;
		w = (xs_u128)m * XS_N[0] + t[0];
		c = (uint64_t)(w >> 64);
		for (j = 1; j < 4; j ++) {
			w = (xs_u128)m * XS_N[j] + t[j] + c;
			t[j - 1] = (uint64_t)w;
			c = (uint64_t)(w >> 64);
		}
		w = (xs_u128)t[4] + c;
		t[3] = (uint64_t)w;
		t[4] = t[5] + (uint64_t)(w >> 64);
	}
	xs_reduce_once(d, t, t[4]);
}

static void
xs_dec32(uint64_t *d, const unsigned char *b)
{
	int i;

	for (i = 0; i < 4; i ++) {
		d[3 - i] = br_dec64be(b + (i << 3));
	}
}

static void
xs_enc32(unsigned char *b, const uint64_t *d)
{
	int i;

	for (i = 0; i < 4; i ++) {
		br_enc64be(b + (i << 3), d[3 - i]);
	}
}

/*
 * s = k^-1 * (m + x*r) mod n. All inputs are 32-byte big-endian integers
 * already reduced modulo n.
 */
static void
xs_p256_sign_tail(unsigned char *sout, const unsigned char *kb,
	const unsigned char *xb, const unsigned char *rb, const unsigned char *mb)
{
	uint64_t k[4], x[4], r[4], m[4], one[4], rr[4], t[4], acc[4];
	uint64_t pw[16][4], e[4], inv, n0i;
	int i, j;

	/* n0i = -1/n mod 2^64 (Newton iteration) */
	inv = 1;
	for (i = 0; i < 6; i ++) {
		inv *= 2 - XS_N[0] * inv;
	}
	n0i = -inv;
	/* one = 2^256 mod n = 2^256 - n; rr = 2^512 mod n by doubling */
	xs_sub4(one, (const uint64_t[4]){ 0, 0, 0, 0 }, XS_N);
	memcpy(rr, one, sizeof rr);
	for (i = 0; i < 256; i ++) {
		xs_addmod(rr, rr, rr);
	}
	xs_dec32(k, kb);
	xs_dec32(x, xb);
	xs_dec32(r, rb);
	xs_dec32(m, mb);

	/* pw[i] = (k*2^256)^i, Montgomery form, i = 0..15 */
	memcpy(pw[0], one, sizeof one);
	xs_montmul(pw[1], k, rr, n0i);
	for (i = 2; i < 16; i ++) {
		xs_montmul(pw[i], pw[i - 1], pw[1], n0i);
	}
	/* acc = k^(n-2) in Montgomery form; exponent read 4 bits at a time from the top */
	memcpy(e, XS_N, sizeof e);
	e[0] -= 2;
	memcpy(acc, one, sizeof acc);
	for (i = 63; i >= 0; i --) {
		unsigned nib = (unsigned)(e[i >> 4] >> ((i & 15) << 2)) & 0x0F;

		for (j = 0; j < 4; j ++) {
			xs_montmul(acc, acc, acc, n0i);
		}
		xs_montmul(acc, acc, pw[nib], n0i);
	}
	/* t = x*r mod n: (x*2^256)*r/2^256 */
	xs_montmul(t, x, rr, n0i);
	xs_montmul(t, t, r, n0i);
	xs_addmod(t, t, m);
	/* s = (k^-1 * 2^256) * t / 2^256 */
	xs_montmul(t, acc, t, n0i);
	xs_enc32(sout, t);
}
#endif

size_t
br_ecdsa_i31_sign_raw(const br_ec_impl *impl,
	const br_hash_class *hf, const void *hash_value,
	const br_ec_private_key *sk, void *sig)
{
	/*
	 * IMPORTANT: this code is fit only for curves with a prime
	 * order. This is needed so that modular reduction of the X
	 * coordinate of a point can be done with a simple subtraction.
	 * We also rely on the last byte of the curve order to be distinct
	 * from 0 and 1.
	 */
	const br_ec_curve_def *cd;
	uint32_t n[I31_LEN], r[I31_LEN], s[I31_LEN], x[I31_LEN];
	uint32_t m[I31_LEN], k[I31_LEN], t1[I31_LEN], t2[I31_LEN];
	unsigned char tt[ORDER_LEN << 1];
	unsigned char eU[POINT_LEN];
	size_t hash_len, nlen, ulen;
	uint32_t n0i, ctl;
	br_hmac_drbg_context drbg;

	/*
	 * If the curve is not supported, then exit with an error.
	 */
	if (((impl->supported_curves >> sk->curve) & 1) == 0) {
		return 0;
	}

	/*
	 * Get the curve parameters (generator and order).
	 */
	switch (sk->curve) {
	case BR_EC_secp256r1:
		cd = &br_secp256r1;
		break;
	case BR_EC_secp384r1:
		cd = &br_secp384r1;
		break;
	case BR_EC_secp521r1:
		cd = &br_secp521r1;
		break;
	default:
		return 0;
	}

	/*
	 * Get modulus.
	 */
	nlen = cd->order_len;
	br_i31_decode(n, cd->order, nlen);
	n0i = br_i31_ninv31(n[1]);

	/*
	 * Get private key as an i31 integer. This also checks that the
	 * private key is well-defined (not zero, and less than the
	 * curve order).
	 */
	if (!br_i31_decode_mod(x, sk->x, sk->xlen, n)) {
		return 0;
	}
	if (br_i31_iszero(x)) {
		return 0;
	}

	/*
	 * Get hash length.
	 */
	hash_len = (hf->desc >> BR_HASHDESC_OUT_OFF) & BR_HASHDESC_OUT_MASK;

	/*
	 * Truncate and reduce the hash value modulo the curve order.
	 */
	br_ecdsa_i31_bits2int(m, hash_value, hash_len, n[0]);
	br_i31_sub(m, n, br_i31_sub(m, n, 0) ^ 1);

	/*
	 * RFC 6979 generation of the "k" value.
	 *
	 * The process uses HMAC_DRBG (with the hash function used to
	 * process the message that is to be signed). The seed is the
	 * concatenation of the encodings of the private key and
	 * the hash value (after truncation and modular reduction).
	 */
	br_i31_encode(tt, nlen, x);
	br_i31_encode(tt + nlen, nlen, m);
	br_hmac_drbg_init(&drbg, hf, tt, nlen << 1);
	for (;;) {
		br_hmac_drbg_generate(&drbg, tt, nlen);
		br_ecdsa_i31_bits2int(k, tt, nlen, n[0]);
		if (br_i31_iszero(k)) {
			continue;
		}
		if (br_i31_sub(k, n, 0)) {
			break;
		}
	}

	/*
	 * Compute k*G and extract the X coordinate, then reduce it
	 * modulo the curve order. Since we support only curves with
	 * prime order, that reduction is only a matter of computing
	 * a subtraction.
	 */
	br_i31_encode(tt, nlen, k);
	ulen = impl->mulgen(eU, tt, nlen, sk->curve);
	br_i31_zero(r, n[0]);
	br_i31_decode(r, &eU[1], ulen >> 1);
	r[0] = n[0];
	br_i31_sub(r, n, br_i31_sub(r, n, 0) ^ 1);

	/*
	 * Compute 1/k in double-Montgomery representation. We do so by
	 * first converting _from_ Montgomery representation (twice),
	 * then using a modular exponentiation.
	 */
#if BR_INT128
	if (sk->curve == BR_EC_secp256r1) {
		unsigned char kb[32], xb[32], rb[32], mb[32];

		br_i31_encode(kb, 32, k);
		br_i31_encode(xb, 32, x);
		br_i31_encode(rb, 32, r);
		br_i31_encode(mb, 32, m);
		br_i31_encode(sig, 32, r);
		xs_p256_sign_tail((unsigned char *)sig + 32, kb, xb, rb, mb);
		br_i31_zero(k, n[0]);
		memset(kb, 0, sizeof kb);
		memset(xb, 0, sizeof xb);
		return 64;
	}
#endif
	br_i31_from_monty(k, n, n0i);
	br_i31_from_monty(k, n, n0i);
	memcpy(tt, cd->order, nlen);
	tt[nlen - 1] -= 2;
	br_i31_modpow(k, tt, nlen, n, n0i, t1, t2);

	/*
	 * Compute s = (m+xr)/k (mod n).
	 * The k[] array contains R^2/k (double-Montgomery representation);
	 * we thus can use direct Montgomery multiplications and conversions
	 * from Montgomery, avoiding any call to br_i31_to_monty() (which
	 * is slower).
	 */
	br_i31_from_monty(m, n, n0i);
	br_i31_montymul(t1, x, r, n, n0i);
	ctl = br_i31_add(t1, m, 1);
	ctl |= br_i31_sub(t1, n, 0) ^ 1;
	br_i31_sub(t1, n, ctl);
	br_i31_montymul(s, t1, k, n, n0i);

	/*
	 * Encode r and s in the signature.
	 */
	br_i31_encode(sig, nlen, r);
	br_i31_encode((unsigned char *)sig + nlen, nlen, s);
	return nlen << 1;
}
