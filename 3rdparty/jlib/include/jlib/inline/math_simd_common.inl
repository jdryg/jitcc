#ifndef JX_MATH_SIMD_H
#error "Must be included from jx/math_simd.h"
#endif

// natural logarithm computed for 4 simultaneous float
// return NaN for x <= 0
static JX_FORCE_INLINE f32x4_t f32x4_log(f32x4_t x)
{
	const uint32_t kMinNormPos = 0x00800000;
	const uint32_t kInvMantMask = 0x807FFFFF;

	const f32x4_t kVec4f_cephes_SQRTHF = f32x4_fromFloat(0.707106781186547524f);
	const f32x4_t kVec4f_cephes_log_p0 = f32x4_fromFloat(7.0376836292E-2f);
	const f32x4_t kVec4f_cephes_log_p1 = f32x4_fromFloat(-1.1514610310E-1f);
	const f32x4_t kVec4f_cephes_log_p2 = f32x4_fromFloat(1.1676998740E-1f);
	const f32x4_t kVec4f_cephes_log_p3 = f32x4_fromFloat(-1.2420140846E-1f);
	const f32x4_t kVec4f_cephes_log_p4 = f32x4_fromFloat(1.4249322787E-1f);
	const f32x4_t kVec4f_cephes_log_p5 = f32x4_fromFloat(-1.6668057665E-1f);
	const f32x4_t kVec4f_cephes_log_p6 = f32x4_fromFloat(2.0000714765E-1f);
	const f32x4_t kVec4f_cephes_log_p7 = f32x4_fromFloat(-2.4999993993E-1f);
	const f32x4_t kVec4f_cephes_log_p8 = f32x4_fromFloat(3.3333331174E-1f);
	const f32x4_t kVec4f_cephes_log_q1 = f32x4_fromFloat(-2.12194440e-4f);
	const f32x4_t kVec4f_cephes_log_q2 = f32x4_fromFloat(0.693359375f);
	const f32x4_t kVec4f_1 = f32x4_fromFloat(1.0f);
	const f32x4_t kVec4f_0p5 = f32x4_fromFloat(0.5f);
	const f32x4_t kVec4f_min_norm_pos = f32x4_fromFloat(*(float*)&kMinNormPos);
	const f32x4_t kVec4f_inv_mant_mask = f32x4_fromFloat(*(float*)&kInvMantMask);
	const i32x4_t kVec4i32_0x7f = i32x4_fromInt(0x7f);

	const f32x4_t invalid_mask = f32x4_cmple(x, f32x4_zero());

	x = f32x4_max(x, kVec4f_min_norm_pos); // cut off denormalized stuff

	i32x4_t emm0 = i32x4_slr(f32x4_castTo_i32x4(x), 23);
	emm0 = i32x4_sub(emm0, kVec4i32_0x7f);
	f32x4_t e = f32x4_from_i32x4(emm0);
	e = f32x4_add(e, kVec4f_1);

	// keep only the fractional part
	x = f32x4_and(x, kVec4f_inv_mant_mask);
	x = f32x4_or(x, kVec4f_0p5);

	/* part2:
	   if( x < SQRTHF ) {
		 e -= 1;
		 x = x + x - 1.0;
	   } else { x = x - 1.0; }
	*/
	f32x4_t mask = f32x4_cmplt(x, kVec4f_cephes_SQRTHF);
	f32x4_t tmp = f32x4_and(x, mask);
	x = f32x4_sub(x, kVec4f_1);
	e = f32x4_sub(e, f32x4_and(kVec4f_1, mask));
	x = f32x4_add(x, tmp);

	f32x4_t z = f32x4_mul(x, x);

	f32x4_t y = kVec4f_cephes_log_p0;
	y = f32x4_madd(y, x, kVec4f_cephes_log_p1);
	y = f32x4_madd(y, x, kVec4f_cephes_log_p2);
	y = f32x4_madd(y, x, kVec4f_cephes_log_p3);
	y = f32x4_madd(y, x, kVec4f_cephes_log_p4);
	y = f32x4_madd(y, x, kVec4f_cephes_log_p5);
	y = f32x4_madd(y, x, kVec4f_cephes_log_p6);
	y = f32x4_madd(y, x, kVec4f_cephes_log_p7);
	y = f32x4_madd(y, x, kVec4f_cephes_log_p8);
	y = f32x4_mul(y, x);
	y = f32x4_mul(y, z);
	y = f32x4_madd(e, kVec4f_cephes_log_q1, y);
	y = f32x4_nmadd(z, kVec4f_0p5, y);

	x = f32x4_add(x, y);
	x = f32x4_add(x, f32x4_mul(e, kVec4f_cephes_log_q2));
	x = f32x4_or(x, invalid_mask); // negative arg will be NAN
	return x;
}

// Implementation taken from Eigen
// The constants are the same as in sse_mathfun but it uses different instructions
// for the polynomial approximation. The final pldexp() from Eigen is not used.
static JX_FORCE_INLINE f32x4_t f32x4_exp(f32x4_t _x)
{
	const f32x4_t kVec4f_exp_hi = f32x4_fromFloat(88.723f);
	const f32x4_t kVec4f_exp_lo = f32x4_fromFloat(-88.723f);
	const f32x4_t kVec4f_cephes_LOG2EF = f32x4_fromFloat(1.44269504088896341f);
	const f32x4_t kVec4f_cephes_exp_C1 = f32x4_fromFloat(0.693359375f);
	const f32x4_t kVec4f_cephes_exp_C2 = f32x4_fromFloat(-2.12194440e-4f);
	const f32x4_t kVec4f_cephes_exp_p0 = f32x4_fromFloat(1.9875691500E-4f);
	const f32x4_t kVec4f_cephes_exp_p1 = f32x4_fromFloat(1.3981999507E-3f);
	const f32x4_t kVec4f_cephes_exp_p2 = f32x4_fromFloat(8.3334519073E-3f);
	const f32x4_t kVec4f_cephes_exp_p3 = f32x4_fromFloat(4.1665795894E-2f);
	const f32x4_t kVec4f_cephes_exp_p4 = f32x4_fromFloat(1.6666665459E-1f);
	const f32x4_t kVec4f_cephes_exp_p5 = f32x4_fromFloat(5.0000001201E-1f);
	const f32x4_t kVec4f_1 = f32x4_fromFloat(1.0f);
	const f32x4_t kVec4f_0p5 = f32x4_fromFloat(0.5f);

	// Clamp x
	f32x4_t x = f32x4_max(f32x4_min(_x, kVec4f_exp_hi), kVec4f_exp_lo);

	// Express exp(x) as exp(m*ln(2) + r), start by extracting
	// m = floor(x/ln(2) + 0.5).
	f32x4_t m = f32x4_floor(f32x4_madd(x, kVec4f_cephes_LOG2EF, kVec4f_0p5));

	// Get r = x - m*ln(2). If no FMA instructions are available, m*ln(2) is
	// subtracted out in two parts, m*C1+m*C2 = m*ln(2), to avoid accumulating
	// truncation errors.
	f32x4_t r = f32x4_nmadd(m, kVec4f_cephes_exp_C1, x);
	r = f32x4_nmadd(m, kVec4f_cephes_exp_C2, r);

	f32x4_t r2 = f32x4_mul(r, r);

#if 1
	f32x4_t r3 = f32x4_mul(r2, r);

	// Eigen: Faster (but worse precision?)
	// Evaluate the polynomial approximant,improved by instruction-level parallelism.
	f32x4_t y, y1, y2;
	y = f32x4_madd(kVec4f_cephes_exp_p0, r, kVec4f_cephes_exp_p1);
	y1 = f32x4_madd(kVec4f_cephes_exp_p3, r, kVec4f_cephes_exp_p4);
	y2 = f32x4_add(r, kVec4f_1);
	y = f32x4_madd(y, r, kVec4f_cephes_exp_p2);
	y1 = f32x4_madd(y1, r, kVec4f_cephes_exp_p5);
	y = f32x4_madd(y, r3, y1);
	y = f32x4_madd(y, r2, y2);
#else
	// sse_mathfun: Slower (but better precision?)
	f32x4_t y = kVec4f_cephes_exp_p0;
	y = f32x4_madd(y, r, kVec4f_cephes_exp_p1);
	y = f32x4_madd(y, r, kVec4f_cephes_exp_p2);
	y = f32x4_madd(y, r, kVec4f_cephes_exp_p3);
	y = f32x4_madd(y, r, kVec4f_cephes_exp_p4);
	y = f32x4_madd(y, r, kVec4f_cephes_exp_p5);
	y = f32x4_madd(y, r2, r);
	y = f32x4_add(y, kVec4f_1);
#endif

	return f32x4_ldexp(y, i32x4_from_f32x4(m));
}

/* evaluation of 4 sines at onces, using only SSE1+MMX intrinsics so
   it runs also on old athlons XPs and the pentium III of your grand
   mother.

   The code is the exact rewriting of the cephes sinf function.
   Precision is excellent as long as x < 8192 (I did not bother to
   take into account the special handling they have for greater values
   -- it does not return garbage for arguments over 8192, though, but
   the extra precision is missing).

   Note that it is such that sinf((float)M_PI) = 8.74e-8, which is the
   surprising but correct result.

   Performance is also surprisingly good, 1.33 times faster than the
   macos vsinf SSE2 function, and 1.5 times faster than the
   __vrs4_sinf of amd's ACML (which is only available in 64 bits). Not
   too bad for an SSE1 function (with no special tuning) !
   However the latter libraries probably have a much better handling of NaN,
   Inf, denormalized and other special arguments..

   On my core 1 duo, the execution of this function takes approximately 95 cycles.

   From what I have observed on the experiments with Intel AMath lib, switching to an
   SSE2 version would improve the perf by only 10%.

   Since it is based on SSE intrinsics, it has to be compiled at -O2 to
   deliver full speed.
*/
static JX_FORCE_INLINE f32x4_t f32x4_sin(f32x4_t x)
{
#if 0
	__m128 xmm1, xmm2 = f32x4_zero(), xmm3, sign_bit, y;

	__m128i emm0, emm2;

	sign_bit = x;
	/* take the absolute value */
	x = f32x4_and(x, *(__m128*)kVec4f_inv_sign_mask);
	/* extract the sign bit (upper one) */
	sign_bit = f32x4_and(sign_bit, *(__m128*)kVec4f_sign_mask);

	/* scale by 4/Pi */
	y = f32x4_mul(x, *(__m128*)kVec4f_cephes_FOPI);

	/* store the integer part of y in mm0 */
	emm2 = i32x4_from_f32x4_truncate(y);
	/* j=(j+1) & (~1) (see the cephes sources) */
	emm2 = i32x4_add(emm2, *(__m128i*)kVec4i32_1);
	emm2 = i32x4_and(emm2, *(__m128i*)kVec4i32_inv1);
	y = f32x4_from_i32x4(emm2);

	/* get the swap sign flag */
	emm0 = i32x4_and(emm2, *(__m128i*)kVec4i32_4);
	emm0 = i32x4_sal(emm0, 29);
	/* get the polynom selection mask
	   there is one polynom for 0 <= x <= Pi/4
	   and another one for Pi/4<x<=Pi/2

	   Both branches will be computed.
	*/
	emm2 = i32x4_and(emm2, *(__m128i*)kVec4i32_2);
	emm2 = i32x4_cmpeq(emm2, i32x4_zero());

	__m128 swap_sign_bit = i32x4_castTo_f32x4(emm0);
	__m128 poly_mask = i32x4_castTo_f32x4(emm2);
	sign_bit = f32x4_xor(sign_bit, swap_sign_bit);

	/* The magic pass: "Extended precision modular arithmetic"
	   x = ((x - y * DP1) - y * DP2) - y * DP3; */
	xmm1 = *(__m128*)kVec4f_minus_cephes_DP1;
	xmm2 = *(__m128*)kVec4f_minus_cephes_DP2;
	xmm3 = *(__m128*)kVec4f_minus_cephes_DP3;
	xmm1 = f32x4_mul(y, xmm1);
	xmm2 = f32x4_mul(y, xmm2);
	xmm3 = f32x4_mul(y, xmm3);
	x = f32x4_add(x, xmm1);
	x = f32x4_add(x, xmm2);
	x = f32x4_add(x, xmm3);

	/* Evaluate the first polynom  (0 <= x <= Pi/4) */
	y = *(__m128*)kVec4f_coscof_p0;
	__m128 z = f32x4_mul(x, x);

	y = f32x4_mul(y, z);
	y = f32x4_add(y, *(__m128*)kVec4f_coscof_p1);
	y = f32x4_mul(y, z);
	y = f32x4_add(y, *(__m128*)kVec4f_coscof_p2);
	y = f32x4_mul(y, z);
	y = f32x4_mul(y, z);
	__m128 tmp = f32x4_mul(z, *(__m128*)kVec4f_0p5);
	y = f32x4_sub(y, tmp);
	y = f32x4_add(y, *(__m128*)kVec4f_1);

	/* Evaluate the second polynom  (Pi/4 <= x <= 0) */

	__m128 y2 = *(__m128*)kVec4f_sincof_p0;
	y2 = f32x4_mul(y2, z);
	y2 = f32x4_add(y2, *(__m128*)kVec4f_sincof_p1);
	y2 = f32x4_mul(y2, z);
	y2 = f32x4_add(y2, *(__m128*)kVec4f_sincof_p2);
	y2 = f32x4_mul(y2, z);
	y2 = f32x4_mul(y2, x);
	y2 = f32x4_add(y2, x);

	/* select the correct result from the two polynoms */
	xmm3 = poly_mask;
	y2 = f32x4_and(xmm3, y2); //, xmm3);
	y = f32x4_andnot(xmm3, y);
	y = f32x4_add(y, y2);
	/* update the sign */
	y = f32x4_xor(y, sign_bit);
	return y;
#else
	return x;
#endif
}

/* almost the same as sin_ps */
static inline f32x4_t f32x4_cos(f32x4_t x)
{
#if 0
	__m128 xmm1, xmm2 = f32x4_zero(), xmm3, y;
	__m128i emm0, emm2;

	/* take the absolute value */
	x = f32x4_and(x, *(__m128*)kVec4f_inv_sign_mask);

	/* scale by 4/Pi */
	y = f32x4_mul(x, *(__m128*)kVec4f_cephes_FOPI);

	/* store the integer part of y in mm0 */
	emm2 = i32x4_from_f32x4_truncate(y);
	/* j=(j+1) & (~1) (see the cephes sources) */
	emm2 = i32x4_add(emm2, *(__m128i*)kVec4i32_1);
	emm2 = i32x4_and(emm2, *(__m128i*)kVec4i32_inv1);
	y = f32x4_from_i32x4(emm2);

	emm2 = i32x4_sub(emm2, *(__m128i*)kVec4i32_2);

	/* get the swap sign flag */
	emm0 = i32x4_andnot(emm2, *(__m128i*)kVec4i32_4);
	emm0 = i32x4_sal(emm0, 29);
	/* get the polynom selection mask */
	emm2 = i32x4_and(emm2, *(__m128i*)kVec4i32_2);
	emm2 = i32x4_cmpeq(emm2, i32x4_zero());

	__m128 sign_bit = i32x4_castTo_f32x4(emm0);
	__m128 poly_mask = i32x4_castTo_f32x4(emm2);

	/* The magic pass: "Extended precision modular arithmetic"
	   x = ((x - y * DP1) - y * DP2) - y * DP3; */
	xmm1 = *(__m128*)kVec4f_minus_cephes_DP1;
	xmm2 = *(__m128*)kVec4f_minus_cephes_DP2;
	xmm3 = *(__m128*)kVec4f_minus_cephes_DP3;
	xmm1 = f32x4_mul(y, xmm1);
	xmm2 = f32x4_mul(y, xmm2);
	xmm3 = f32x4_mul(y, xmm3);
	x = f32x4_add(x, xmm1);
	x = f32x4_add(x, xmm2);
	x = f32x4_add(x, xmm3);

	/* Evaluate the first polynom  (0 <= x <= Pi/4) */
	y = *(__m128*)kVec4f_coscof_p0;
	__m128 z = f32x4_mul(x, x);

	y = f32x4_mul(y, z);
	y = f32x4_add(y, *(__m128*)kVec4f_coscof_p1);
	y = f32x4_mul(y, z);
	y = f32x4_add(y, *(__m128*)kVec4f_coscof_p2);
	y = f32x4_mul(y, z);
	y = f32x4_mul(y, z);
	__m128 tmp = f32x4_mul(z, *(__m128*)kVec4f_0p5);
	y = f32x4_sub(y, tmp);
	y = f32x4_add(y, *(__m128*)kVec4f_1);

	/* Evaluate the second polynom  (Pi/4 <= x <= 0) */

	__m128 y2 = *(__m128*)kVec4f_sincof_p0;
	y2 = f32x4_mul(y2, z);
	y2 = f32x4_add(y2, *(__m128*)kVec4f_sincof_p1);
	y2 = f32x4_mul(y2, z);
	y2 = f32x4_add(y2, *(__m128*)kVec4f_sincof_p2);
	y2 = f32x4_mul(y2, z);
	y2 = f32x4_mul(y2, x);
	y2 = f32x4_add(y2, x);

	/* select the correct result from the two polynoms */
	xmm3 = poly_mask;
	y2 = f32x4_and(xmm3, y2); //, xmm3);
	y = f32x4_andnot(xmm3, y);
	y = f32x4_add(y, y2);
	/* update the sign */
	y = f32x4_xor(y, sign_bit);

	return y;
#else
	return x;
#endif
}

// since sin_ps and cos_ps are almost identical, sincos_ps could replace both of them..
// it is almost as fast, and gives you a free cosine with your sine */
static JX_FORCE_INLINE void f32x4_sincos(f32x4_t x, f32x4_t* s, f32x4_t* c)
{
	const f32x4_t kVec4f_minus_cephes_DP1 = f32x4_fromFloat(-0.78515625);
	const f32x4_t kVec4f_minus_cephes_DP2 = f32x4_fromFloat(-2.4187564849853515625e-4);
	const f32x4_t kVec4f_minus_cephes_DP3 = f32x4_fromFloat(-3.77489497744594108e-8f);
	const f32x4_t kVec4f_sincof_p0 = f32x4_fromFloat(-1.9515295891E-4f);
	const f32x4_t kVec4f_sincof_p1 = f32x4_fromFloat(8.3321608736E-3f);
	const f32x4_t kVec4f_sincof_p2 = f32x4_fromFloat(-1.6666654611E-1f);
	const f32x4_t kVec4f_coscof_p0 = f32x4_fromFloat(2.443315711809948E-005f);
	const f32x4_t kVec4f_coscof_p1 = f32x4_fromFloat(-1.388731625493765E-003f);
	const f32x4_t kVec4f_coscof_p2 = f32x4_fromFloat(4.166664568298827E-002f);
	const f32x4_t kVec4f_cephes_FOPI = f32x4_fromFloat(1.27323954473516f); // 4 / M_PI
	const f32x4_t kVec4f_1 = f32x4_fromFloat(1.0f);
	const f32x4_t kVec4f_0p5 = f32x4_fromFloat(0.5f);
	const i32x4_t kVec4i32_1 = i32x4_fromInt(1);
	const i32x4_t kVec4i32_2 = i32x4_fromInt(2);
	const i32x4_t kVec4i32_4 = i32x4_fromInt(4);
	const i32x4_t kVec4i32_inv1 = i32x4_fromInt(~1);

	// extract the sign bit (upper one)
	f32x4_t sign_bit_sin = f32x4_getSignMask(x);
	
	// take the absolute value
	x = f32x4_abs(x);

	// scale by 4/Pi
	f32x4_t y = f32x4_mul(x, kVec4f_cephes_FOPI);

	// store the integer part of y in emm2
	i32x4_t emm2 = i32x4_from_f32x4_truncate(y);

	// j=(j+1) & (~1) (see the cephes sources)
	emm2 = i32x4_add(emm2, kVec4i32_1);
	emm2 = i32x4_and(emm2, kVec4i32_inv1);
	y = f32x4_from_i32x4(emm2);

	i32x4_t emm4 = emm2;

	// get the swap sign flag for the sine
	i32x4_t emm0 = i32x4_and(emm2, kVec4i32_4);
	emm0 = i32x4_sal(emm0, 29);
	f32x4_t swap_sign_bit_sin = i32x4_castTo_f32x4(emm0);

	// get the polynom selection mask for the sine */
	emm2 = i32x4_and(emm2, kVec4i32_2);
	emm2 = i32x4_cmpeq(emm2, i32x4_zero());
	f32x4_t poly_mask = i32x4_castTo_f32x4(emm2);

	// The magic pass: "Extended precision modular arithmetic"
	//   x = ((x - y * DP1) - y * DP2) - y * DP3; */
	x = f32x4_madd(y, kVec4f_minus_cephes_DP1, x);
	x = f32x4_madd(y, kVec4f_minus_cephes_DP2, x);
	x = f32x4_madd(y, kVec4f_minus_cephes_DP3, x);

	emm4 = i32x4_sub(emm4, kVec4i32_2);
	emm4 = i32x4_andnot(emm4, kVec4i32_4);
	emm4 = i32x4_sal(emm4, 29);
	f32x4_t sign_bit_cos = i32x4_castTo_f32x4(emm4);

	sign_bit_sin = f32x4_xor(sign_bit_sin, swap_sign_bit_sin);

	f32x4_t z = f32x4_mul(x, x);

	// Evaluate the first polynom  (0 <= x <= Pi/4)
	y = kVec4f_coscof_p0;
	y = f32x4_madd(y, z, kVec4f_coscof_p1);
	y = f32x4_madd(y, z, kVec4f_coscof_p2);
	y = f32x4_mul(y, z);
	y = f32x4_mul(y, z);
	y = f32x4_nmadd(z, kVec4f_0p5, y);
	y = f32x4_add(y, kVec4f_1);

	// Evaluate the second polynom  (Pi/4 <= x <= 0) */
	f32x4_t y2 = kVec4f_sincof_p0;
	y2 = f32x4_madd(y2, z, kVec4f_sincof_p1);
	y2 = f32x4_madd(y2, z, kVec4f_sincof_p2);
	y2 = f32x4_mul(y2, z);
	y2 = f32x4_madd(y2, x, x);

	// select the correct result from the two polynoms
	f32x4_t ysin2 = f32x4_and(poly_mask, y2);
	f32x4_t ysin1 = f32x4_andnot(poly_mask, y);
	y2 = f32x4_sub(y2, ysin2);
	y = f32x4_sub(y, ysin1);

	f32x4_t xmm1 = f32x4_add(ysin1, ysin2);
	f32x4_t xmm2 = f32x4_add(y, y2);

	/* update the sign */
	*s = f32x4_xor(xmm1, sign_bit_sin);
	*c = f32x4_xor(xmm2, sign_bit_cos);
}

static JX_FORCE_INLINE f32x4_t f32x4_sinh(f32x4_t x)
{
	// sinh(x) = (exp(2x) - 1) / (2exp(x))
	const f32x4_t exp_x = f32x4_exp(x);
	return f32x4_div(f32x4_msub(exp_x, exp_x, f32x4_fromFloat(1.0f)), f32x4_add(exp_x, exp_x));
}

static JX_FORCE_INLINE f32x4_t f32x4_cosh(f32x4_t x)
{
	// sinh(x) = (exp(2x) + 1) / (2exp(x))
	const f32x4_t exp_x = f32x4_exp(x);
	return f32x4_div(f32x4_madd(exp_x, exp_x, f32x4_fromFloat(1.0f)), f32x4_add(exp_x, exp_x));
}

static JX_FORCE_INLINE void f32x4_sincosh(f32x4_t x, f32x4_t* s, f32x4_t* c)
{
	const f32x4_t exp_x = f32x4_exp(x);
	const f32x4_t exp_x2 = f32x4_add(exp_x, exp_x);
	const f32x4_t exp_x_sqr = f32x4_mul(exp_x, exp_x);
	*s = f32x4_div(f32x4_sub(exp_x_sqr, f32x4_fromFloat(1.0f)), exp_x2);
	*c = f32x4_div(f32x4_add(exp_x_sqr, f32x4_fromFloat(1.0f)), exp_x2);
}

static JX_FORCE_INLINE f32x4_t f32x4_atan2(f32x4_t y, f32x4_t x)
{
	const uint32_t kSignMask = 0x80000000;

	const f32x4_t kVec4f_cephes_PIF = f32x4_fromFloat(3.141592653589793238f);
	const f32x4_t kVec4f_cephes_PIO2F = f32x4_fromFloat(1.5707963267948966192f);
	const f32x4_t kVec4f_sign_mask = f32x4_fromFloat(*(float*)&kSignMask);

	f32x4_t x_eq_0 = f32x4_cmpeq(x, f32x4_zero());
	f32x4_t x_gt_0 = f32x4_cmpgt(x, f32x4_zero());
	f32x4_t x_le_0 = f32x4_cmple(x, f32x4_zero());
	f32x4_t y_eq_0 = f32x4_cmpeq(y, f32x4_zero());
	f32x4_t x_lt_0 = f32x4_cmplt(x, f32x4_zero());
	f32x4_t y_lt_0 = f32x4_cmplt(y, f32x4_zero());

	f32x4_t zero_mask = f32x4_and(x_eq_0, y_eq_0);
	f32x4_t zero_mask_other_case = f32x4_and(y_eq_0, x_gt_0);
	zero_mask = f32x4_or(zero_mask, zero_mask_other_case);

	f32x4_t pio2_mask = f32x4_andnot(y_eq_0, x_eq_0);
	f32x4_t pio2_mask_sign = f32x4_and(y_lt_0, kVec4f_sign_mask);
	f32x4_t pio2_result = kVec4f_cephes_PIO2F;
	pio2_result = f32x4_xor(pio2_result, pio2_mask_sign);
	pio2_result = f32x4_and(pio2_mask, pio2_result);

	f32x4_t pi_mask = f32x4_and(y_eq_0, x_le_0);
	f32x4_t pi = kVec4f_cephes_PIF;
	f32x4_t pi_result = f32x4_and(pi_mask, pi);

	f32x4_t swap_sign_mask_offset = f32x4_and(x_lt_0, y_lt_0);
	swap_sign_mask_offset = f32x4_and(swap_sign_mask_offset, kVec4f_sign_mask);

	f32x4_t offset0 = f32x4_zero();
	f32x4_t offset1 = kVec4f_cephes_PIF;
	offset1 = f32x4_xor(offset1, swap_sign_mask_offset);

	f32x4_t offset = f32x4_andnot(x_lt_0, offset0);
	offset = f32x4_and(x_lt_0, offset1);

	f32x4_t arg = f32x4_div(y, x);
	f32x4_t atan_result = f32x4_atan(arg);
	atan_result = f32x4_add(atan_result, offset);

	// select between zero_result, pio2_result and atan_result

	f32x4_t result = f32x4_andnot(zero_mask, pio2_result);
	atan_result = f32x4_andnot(pio2_mask, atan_result);
	atan_result = f32x4_andnot(pio2_mask, atan_result);
	result = f32x4_or(result, atan_result);
	result = f32x4_or(result, pi_result);

	return result;
}

static JX_FORCE_INLINE f32x4_t f32x4_atan(f32x4_t x)
{
	const uint32_t kInvSignMask = 0x7FFFFFFF;
	const uint32_t kSignMask = 0x80000000;

	const f32x4_t kVec4f_inv_sign_mask = f32x4_fromFloat(*(float*)&kInvSignMask);
	const f32x4_t kVec4f_sign_mask = f32x4_fromFloat(*(float*)&kSignMask);
	const f32x4_t kVec4f_atanrange_hi = f32x4_fromFloat(2.414213562373095f);
	const f32x4_t kVec4f_atanrange_lo = f32x4_fromFloat(0.4142135623730950f);
	const f32x4_t kVec4f_atancof_p0 = f32x4_fromFloat(8.05374449538e-2f);
	const f32x4_t kVec4f_atancof_p1 = f32x4_fromFloat(1.38776856032E-1f);
	const f32x4_t kVec4f_atancof_p2 = f32x4_fromFloat(1.99777106478E-1f);
	const f32x4_t kVec4f_atancof_p3 = f32x4_fromFloat(3.33329491539E-1f);
	const f32x4_t kVec4f_cephes_PIO2F = f32x4_fromFloat(1.5707963267948966192f);
	const f32x4_t kVec4f_cephes_PIO4F = f32x4_fromFloat(0.7853981633974483096f);
	const f32x4_t kVec4f_1 = f32x4_fromFloat(1.0f);

	f32x4_t sign_bit = x;

	// take the absolute value
	x = f32x4_and(x, kVec4f_inv_sign_mask);

	// extract the sign bit (upper one)
	sign_bit = f32x4_and(sign_bit, kVec4f_sign_mask);

	// range reduction, init x and y depending on range
	// x > 2.414213562373095
	f32x4_t cmp0 = f32x4_cmpgt(x, kVec4f_atanrange_hi);
	// x > 0.4142135623730950
	f32x4_t cmp1 = f32x4_cmpgt(x, kVec4f_atanrange_lo);

	// x > 0.4142135623730950 && !( x > 2.414213562373095 )
	f32x4_t cmp2 = f32x4_andnot(cmp0, cmp1);

	// -( 1.0/x )
	f32x4_t y0 = f32x4_and(cmp0, kVec4f_cephes_PIO2F);
	f32x4_t x0 = f32x4_div(kVec4f_1, x);
	x0 = f32x4_xor(x0, kVec4f_sign_mask);

	f32x4_t y1 = f32x4_and(cmp2, kVec4f_cephes_PIO4F);

	// (x-1.0)/(x+1.0)
	f32x4_t x1_o = f32x4_sub(x, kVec4f_1);
	f32x4_t x1_u = f32x4_add(x, kVec4f_1);
	f32x4_t x1 = f32x4_div(x1_o, x1_u);

	f32x4_t x2 = f32x4_and(cmp2, x1);
	x0 = f32x4_and(cmp0, x0);
	x2 = f32x4_or(x2, x0);
	cmp1 = f32x4_or(cmp0, cmp2);
	x2 = f32x4_and(cmp1, x2);
	x = f32x4_andnot(cmp1, x);
	x = f32x4_or(x2, x);

	f32x4_t y = f32x4_or(y0, y1);

	f32x4_t zz = f32x4_mul(x, x);
	f32x4_t acc = kVec4f_atancof_p0;
	acc = f32x4_msub(acc, zz, kVec4f_atancof_p1);
	acc = f32x4_madd(acc, zz, kVec4f_atancof_p2);
	acc = f32x4_msub(acc, zz, kVec4f_atancof_p3);
	acc = f32x4_mul(acc, zz);
	acc = f32x4_madd(acc, x, x);
	y = f32x4_add(y, acc);

	// update the sign
	y = f32x4_xor(y, sign_bit);

	return y;
}

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE f32x8_t f32x8_log(f32x8_t x)
{
	const uint32_t kMinNormPos = 0x00800000;
	const uint32_t kInvMantMask = 0x807FFFFF;

	const f32x8_t kVec8f_cephes_SQRTHF = f32x8_fromFloat(0.707106781186547524f);
	const f32x8_t kVec8f_cephes_log_p0 = f32x8_fromFloat(7.0376836292E-2f);
	const f32x8_t kVec8f_cephes_log_p1 = f32x8_fromFloat(-1.1514610310E-1f);
	const f32x8_t kVec8f_cephes_log_p2 = f32x8_fromFloat(1.1676998740E-1f);
	const f32x8_t kVec8f_cephes_log_p3 = f32x8_fromFloat(-1.2420140846E-1f);
	const f32x8_t kVec8f_cephes_log_p4 = f32x8_fromFloat(1.4249322787E-1f);
	const f32x8_t kVec8f_cephes_log_p5 = f32x8_fromFloat(-1.6668057665E-1f);
	const f32x8_t kVec8f_cephes_log_p6 = f32x8_fromFloat(2.0000714765E-1f);
	const f32x8_t kVec8f_cephes_log_p7 = f32x8_fromFloat(-2.4999993993E-1f);
	const f32x8_t kVec8f_cephes_log_p8 = f32x8_fromFloat(3.3333331174E-1f);
	const f32x8_t kVec8f_cephes_log_q1 = f32x8_fromFloat(-2.12194440e-4f);
	const f32x8_t kVec8f_cephes_log_q2 = f32x8_fromFloat(0.693359375f);
	const f32x8_t kVec8f_1 = f32x8_fromFloat(1.0f);
	const f32x8_t kVec8f_0p5 = f32x8_fromFloat(0.5f);
	const f32x8_t kVec8f_min_norm_pos = f32x8_fromFloat(*(float*)&kMinNormPos);
	const f32x8_t kVec8f_inv_mant_mask = f32x8_fromFloat(*(float*)&kInvMantMask);
	const i32x8_t kVec8i32_0x7f = i32x8_fromInt(0x7f);

	f32x8_t invalid_mask = f32x8_cmple(x, f32x8_zero());

	x = f32x8_max(x, kVec8f_min_norm_pos);  /* cut off denormalized stuff */

	i32x8_t emm0 = i32x8_slr(f32x8_castTo_i32x8(x), 23);
	/* keep only the fractional part */
	x = f32x8_and(x, kVec8f_inv_mant_mask);
	x = f32x8_or(x, kVec8f_0p5);

	emm0 = i32x8_sub(emm0, kVec8i32_0x7f);
	f32x8_t e = f32x8_from_i32x8(emm0);

	e = f32x8_add(e, kVec8f_1);

	/* part2:
	   if( x < SQRTHF ) {
		 e -= 1;
		 x = x + x - 1.0;
	   } else { x = x - 1.0; }
	*/
	f32x8_t mask = f32x8_cmplt(x, kVec8f_cephes_SQRTHF);
	f32x8_t tmp = f32x8_and(x, mask);
	x = f32x8_sub(x, kVec8f_1);
	e = f32x8_sub(e, f32x8_and(kVec8f_1, mask));
	x = f32x8_add(x, tmp);

	f32x8_t z = f32x8_mul(x, x);

	f32x8_t y = kVec8f_cephes_log_p0;
	y = f32x8_madd(y, x, kVec8f_cephes_log_p1);
	y = f32x8_madd(y, x, kVec8f_cephes_log_p2);
	y = f32x8_madd(y, x, kVec8f_cephes_log_p3);
	y = f32x8_madd(y, x, kVec8f_cephes_log_p4);
	y = f32x8_madd(y, x, kVec8f_cephes_log_p5);
	y = f32x8_madd(y, x, kVec8f_cephes_log_p6);
	y = f32x8_madd(y, x, kVec8f_cephes_log_p7);
	y = f32x8_madd(y, x, kVec8f_cephes_log_p8);
	y = f32x8_mul(y, x);
	y = f32x8_mul(y, z);
	y = f32x8_madd(e, kVec8f_cephes_log_q1, y);
	y = f32x8_nmadd(z, kVec8f_0p5, y);

	x = f32x8_add(x, y);
	x = f32x8_add(x, f32x8_mul(e, kVec8f_cephes_log_q2));
	x = f32x8_or(x, invalid_mask); // negative arg will be NAN
	return x;
}
#endif

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE f32x8_t f32x8_exp(f32x8_t _x)
{
	const f32x8_t kVec8f_exp_hi = f32x8_fromFloat(88.723f);
	const f32x8_t kVec8f_exp_lo = f32x8_fromFloat(-88.723f);
	const f32x8_t kVec8f_cephes_LOG2EF = f32x8_fromFloat(1.44269504088896341f);
	const f32x8_t kVec8f_cephes_exp_C1 = f32x8_fromFloat(0.693359375f);
	const f32x8_t kVec8f_cephes_exp_C2 = f32x8_fromFloat(-2.12194440e-4f);
	const f32x8_t kVec8f_cephes_exp_p0 = f32x8_fromFloat(1.9875691500E-4f);
	const f32x8_t kVec8f_cephes_exp_p1 = f32x8_fromFloat(1.3981999507E-3f);
	const f32x8_t kVec8f_cephes_exp_p2 = f32x8_fromFloat(8.3334519073E-3f);
	const f32x8_t kVec8f_cephes_exp_p3 = f32x8_fromFloat(4.1665795894E-2f);
	const f32x8_t kVec8f_cephes_exp_p4 = f32x8_fromFloat(1.6666665459E-1f);
	const f32x8_t kVec8f_cephes_exp_p5 = f32x8_fromFloat(5.0000001201E-1f);
	const f32x8_t kVec8f_1 = f32x8_fromFloat(1.0f);
	const f32x8_t kVec8f_0p5 = f32x8_fromFloat(0.5f);

	// Clamp x
	f32x8_t x = f32x8_max(f32x8_min(_x, kVec8f_exp_hi), kVec8f_exp_lo);

	// Express exp(x) as exp(m*ln(2) + r), start by extracting
	// m = floor(x/ln(2) + 0.5).
	f32x8_t m = f32x8_floor(f32x8_madd(x, kVec8f_cephes_LOG2EF, kVec8f_0p5));

	// Get r = x - m*ln(2). If no FMA instructions are available, m*ln(2) is
	// subtracted out in two parts, m*C1+m*C2 = m*ln(2), to avoid accumulating
	// truncation errors.
	f32x8_t r = f32x8_nmadd(m, kVec8f_cephes_exp_C1, x);
	r = f32x8_nmadd(m, kVec8f_cephes_exp_C2, r);

	f32x8_t r2 = f32x8_mul(r, r);

#if 1
	f32x8_t r3 = f32x8_mul(r2, r);

	// Eigen: Faster (but worse precision?)
	// Evaluate the polynomial approximant,improved by instruction-level parallelism.
	f32x8_t y, y1, y2;
	y = f32x8_madd(kVec8f_cephes_exp_p0, r, kVec8f_cephes_exp_p1);
	y1 = f32x8_madd(kVec8f_cephes_exp_p3, r, kVec8f_cephes_exp_p4);
	y2 = f32x8_add(r, kVec8f_1);
	y = f32x8_madd(y, r, kVec8f_cephes_exp_p2);
	y1 = f32x8_madd(y1, r, kVec8f_cephes_exp_p5);
	y = f32x8_madd(y, r3, y1);
	y = f32x8_madd(y, r2, y2);
#else
	// sse_mathfun: Slower (but better precision?)
	f32x8_t y = kVec8f_cephes_exp_p0;
	y = f32x8_madd(y, r, kVec8f_cephes_exp_p1);
	y = f32x8_madd(y, r, kVec8f_cephes_exp_p2);
	y = f32x8_madd(y, r, kVec8f_cephes_exp_p3);
	y = f32x8_madd(y, r, kVec8f_cephes_exp_p4);
	y = f32x8_madd(y, r, kVec8f_cephes_exp_p5);
	y = f32x8_madd(y, r2, r);
	y = f32x8_add(y, kVec8f_1);
#endif

	return f32x8_ldexp(y, i32x8_from_f32x8_truncate(m));
}
#endif

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE void f32x8_sincos(f32x8_t x, f32x8_t* s, f32x8_t* c)
{
	const f32x8_t kVec8f_minus_cephes_DP1 = f32x8_fromFloat(-0.78515625);
	const f32x8_t kVec8f_minus_cephes_DP2 = f32x8_fromFloat(-2.4187564849853515625e-4);
	const f32x8_t kVec8f_minus_cephes_DP3 = f32x8_fromFloat(-3.77489497744594108e-8f);
	const f32x8_t kVec8f_sincof_p0 = f32x8_fromFloat(-1.9515295891E-4f);
	const f32x8_t kVec8f_sincof_p1 = f32x8_fromFloat(8.3321608736E-3f);
	const f32x8_t kVec8f_sincof_p2 = f32x8_fromFloat(-1.6666654611E-1f);
	const f32x8_t kVec8f_coscof_p0 = f32x8_fromFloat(2.443315711809948E-005f);
	const f32x8_t kVec8f_coscof_p1 = f32x8_fromFloat(-1.388731625493765E-003f);
	const f32x8_t kVec8f_coscof_p2 = f32x8_fromFloat(4.166664568298827E-002f);
	const f32x8_t kVec8f_cephes_FOPI = f32x8_fromFloat(1.27323954473516f); // 4 / M_PI
	const f32x8_t kVec8f_1 = f32x8_fromFloat(1.0f);
	const f32x8_t kVec8f_0p5 = f32x8_fromFloat(0.5f);
	const i32x8_t kVec8i32_1 = i32x8_fromInt(1);
	const i32x8_t kVec8i32_2 = i32x8_fromInt(2);
	const i32x8_t kVec8i32_4 = i32x8_fromInt(4);
	const i32x8_t kVec8i32_inv1 = i32x8_fromInt(~1);

	// extract the sign bit (upper one)
	f32x8_t sign_bit_sin = f32x8_getSignMask(x);

	// take the absolute value
	x = f32x8_abs(x);

	// scale by 4/Pi
	f32x8_t y = f32x8_mul(x, kVec8f_cephes_FOPI);

	// store the integer part of y in emm2
	i32x8_t emm2 = i32x8_from_f32x8_truncate(y);

	// j=(j+1) & (~1) (see the cephes sources)
	emm2 = i32x8_add(emm2, kVec8i32_1);
	emm2 = i32x8_and(emm2, kVec8i32_inv1);
	y = f32x8_from_i32x8(emm2);

	i32x8_t emm4 = emm2;

	// get the swap sign flag for the sine
	i32x8_t emm0 = i32x8_and(emm2, kVec8i32_4);
	emm0 = i32x8_sal(emm0, 29);
	f32x8_t swap_sign_bit_sin = i32x8_castToVec8f(emm0);

	// get the polynom selection mask for the sine */
	emm2 = i32x8_and(emm2, kVec8i32_2);
	emm2 = i32x8_cmpeq(emm2, i32x8_zero());
	f32x8_t poly_mask = i32x8_castToVec8f(emm2);

	// The magic pass: "Extended precision modular arithmetic"
	//   x = ((x - y * DP1) - y * DP2) - y * DP3; */
	x = f32x8_madd(y, kVec8f_minus_cephes_DP1, x);
	x = f32x8_madd(y, kVec8f_minus_cephes_DP2, x);
	x = f32x8_madd(y, kVec8f_minus_cephes_DP3, x);

	emm4 = i32x8_sub(emm4, kVec8i32_2);
	emm4 = i32x8_andnot(emm4, kVec8i32_4);
	emm4 = i32x8_sal(emm4, 29);
	f32x8_t sign_bit_cos = i32x8_castToVec8f(emm4);

	sign_bit_sin = f32x8_xor(sign_bit_sin, swap_sign_bit_sin);

	f32x8_t z = f32x8_mul(x, x);

	// Evaluate the first polynom  (0 <= x <= Pi/4)
	y = kVec8f_coscof_p0;
	y = f32x8_madd(y, z, kVec8f_coscof_p1);
	y = f32x8_madd(y, z, kVec8f_coscof_p2);
	y = f32x8_mul(y, z);
	y = f32x8_mul(y, z);
	y = f32x8_nmadd(z, kVec8f_0p5, y);
	y = f32x8_add(y, kVec8f_1);

	// Evaluate the second polynom  (Pi/4 <= x <= 0) */
	f32x8_t y2 = kVec8f_sincof_p0;
	y2 = f32x8_madd(y2, z, kVec8f_sincof_p1);
	y2 = f32x8_madd(y2, z, kVec8f_sincof_p2);
	y2 = f32x8_mul(y2, z);
	y2 = f32x8_madd(y2, x, x);

	// select the correct result from the two polynoms
	f32x8_t ysin2 = f32x8_and(poly_mask, y2);
	f32x8_t ysin1 = f32x8_andnot(poly_mask, y);
	y2 = f32x8_sub(y2, ysin2);
	y = f32x8_sub(y, ysin1);

	f32x8_t xmm1 = f32x8_add(ysin1, ysin2);
	f32x8_t xmm2 = f32x8_add(y, y2);

	/* update the sign */
	*s = f32x8_xor(xmm1, sign_bit_sin);
	*c = f32x8_xor(xmm2, sign_bit_cos);
}
#endif // defined(JX_MATH_SIMD_AVX2)

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE f32x8_t f32x8_sinh(f32x8_t x)
{
	// sinh(x) = (exp(2x) - 1) / (2exp(x))
	const f32x8_t exp_x = f32x8_exp(x);
	return f32x8_div(f32x8_msub(exp_x, exp_x, f32x8_fromFloat(1.0f)), f32x8_add(exp_x, exp_x));
}
#endif // defined(JX_MATH_SIMD_AVX2)

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE f32x8_t f32x8_cosh(f32x8_t x)
{
	// sinh(x) = (exp(2x) + 1) / (2exp(x))
	const f32x8_t exp_x = f32x8_exp(x);
	return f32x8_div(f32x8_madd(exp_x, exp_x, f32x8_fromFloat(1.0f)), f32x8_add(exp_x, exp_x));
}
#endif // defined(JX_MATH_SIMD_AVX2)

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE void f32x8_sincosh(f32x8_t x, f32x8_t* s, f32x8_t* c)
{
	const f32x8_t exp_x = f32x8_exp(x);
	const f32x8_t exp_x2 = f32x8_add(exp_x, exp_x);
	const f32x8_t exp_x_sqr = f32x8_mul(exp_x, exp_x);
	*s = f32x8_div(f32x8_sub(exp_x_sqr, f32x8_fromFloat(1.0f)), exp_x2);
	*c = f32x8_div(f32x8_add(exp_x_sqr, f32x8_fromFloat(1.0f)), exp_x2);
}
#endif // defined(JX_MATH_SIMD_AVX2)

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE f32x8_t f32x8_atan2(f32x8_t y, f32x8_t x)
{
	const uint32_t kSignMask = 0x80000000;

	const f32x8_t kVec8f_cephes_PIF = f32x8_fromFloat(3.141592653589793238f);
	const f32x8_t kVec8f_cephes_PIO2F = f32x8_fromFloat(1.5707963267948966192f);
	const f32x8_t kVec8f_sign_mask = f32x8_fromFloat(*(float*)&kSignMask);

	f32x8_t x_eq_0 = f32x8_cmpeq(x, f32x8_zero());
	f32x8_t x_gt_0 = f32x8_cmpgt(x, f32x8_zero());
	f32x8_t x_le_0 = f32x8_cmple(x, f32x8_zero());
	f32x8_t y_eq_0 = f32x8_cmpeq(y, f32x8_zero());
	f32x8_t x_lt_0 = f32x8_cmplt(x, f32x8_zero());
	f32x8_t y_lt_0 = f32x8_cmplt(y, f32x8_zero());

	f32x8_t zero_mask = f32x8_and(x_eq_0, y_eq_0);
	f32x8_t zero_mask_other_case = f32x8_and(y_eq_0, x_gt_0);
	zero_mask = f32x8_or(zero_mask, zero_mask_other_case);

	f32x8_t pio2_mask = f32x8_andnot(y_eq_0, x_eq_0);
	f32x8_t pio2_mask_sign = f32x8_and(y_lt_0, kVec8f_sign_mask);
	f32x8_t pio2_result = kVec8f_cephes_PIO2F;
	pio2_result = f32x8_xor(pio2_result, pio2_mask_sign);
	pio2_result = f32x8_and(pio2_mask, pio2_result);

	f32x8_t pi_mask = f32x8_and(y_eq_0, x_le_0);
	f32x8_t pi = kVec8f_cephes_PIF;
	f32x8_t pi_result = f32x8_and(pi_mask, pi);

	f32x8_t swap_sign_mask_offset = f32x8_and(x_lt_0, y_lt_0);
	swap_sign_mask_offset = f32x8_and(swap_sign_mask_offset, kVec8f_sign_mask);

	f32x8_t offset0 = f32x8_zero();
	f32x8_t offset1 = kVec8f_cephes_PIF;
	offset1 = f32x8_xor(offset1, swap_sign_mask_offset);

	f32x8_t offset = f32x8_andnot(x_lt_0, offset0);
	offset = f32x8_and(x_lt_0, offset1);

	f32x8_t arg = f32x8_div(y, x);
	f32x8_t atan_result = f32x8_atan(arg);
	atan_result = f32x8_add(atan_result, offset);

	// select between zero_result, pio2_result and atan_result

	f32x8_t result = f32x8_andnot(zero_mask, pio2_result);
	atan_result = f32x8_andnot(pio2_mask, atan_result);
	atan_result = f32x8_andnot(pio2_mask, atan_result);
	result = f32x8_or(result, atan_result);
	result = f32x8_or(result, pi_result);

	return result;
}
#endif // defined(JX_MATH_SIMD_AVX2)

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE f32x8_t f32x8_atan(f32x8_t x)
{
	const uint32_t kInvSignMask = 0x7FFFFFFF;
	const uint32_t kSignMask = 0x80000000;

	const f32x8_t kVec8f_inv_sign_mask = f32x8_fromFloat(*(float*)&kInvSignMask);
	const f32x8_t kVec8f_sign_mask = f32x8_fromFloat(*(float*)&kSignMask);
	const f32x8_t kVec8f_atanrange_hi = f32x8_fromFloat(2.414213562373095f);
	const f32x8_t kVec8f_atanrange_lo = f32x8_fromFloat(0.4142135623730950f);
	const f32x8_t kVec8f_atancof_p0 = f32x8_fromFloat(8.05374449538e-2f);
	const f32x8_t kVec8f_atancof_p1 = f32x8_fromFloat(1.38776856032E-1f);
	const f32x8_t kVec8f_atancof_p2 = f32x8_fromFloat(1.99777106478E-1f);
	const f32x8_t kVec8f_atancof_p3 = f32x8_fromFloat(3.33329491539E-1f);
	const f32x8_t kVec8f_cephes_PIO2F = f32x8_fromFloat(1.5707963267948966192f);
	const f32x8_t kVec8f_cephes_PIO4F = f32x8_fromFloat(0.7853981633974483096f);
	const f32x8_t kVec8f_1 = f32x8_fromFloat(1.0f);

	f32x8_t sign_bit = x;

	// take the absolute value
	x = f32x8_and(x, kVec8f_inv_sign_mask);

	// extract the sign bit (upper one)
	sign_bit = f32x8_and(sign_bit, kVec8f_sign_mask);

	// range reduction, init x and y depending on range
	// x > 2.414213562373095
	f32x8_t cmp0 = f32x8_cmpgt(x, kVec8f_atanrange_hi);
	// x > 0.4142135623730950
	f32x8_t cmp1 = f32x8_cmpgt(x, kVec8f_atanrange_lo);

	// x > 0.4142135623730950 && !( x > 2.414213562373095 )
	f32x8_t cmp2 = f32x8_andnot(cmp0, cmp1);

	// -( 1.0/x )
	f32x8_t y0 = f32x8_and(cmp0, kVec8f_cephes_PIO2F);
	f32x8_t x0 = f32x8_div(kVec8f_1, x);
	x0 = f32x8_xor(x0, kVec8f_sign_mask);

	f32x8_t y1 = f32x8_and(cmp2, kVec8f_cephes_PIO4F);

	// (x-1.0)/(x+1.0)
	f32x8_t x1_o = f32x8_sub(x, kVec8f_1);
	f32x8_t x1_u = f32x8_add(x, kVec8f_1);
	f32x8_t x1 = f32x8_div(x1_o, x1_u);

	f32x8_t x2 = f32x8_and(cmp2, x1);
	x0 = f32x8_and(cmp0, x0);
	x2 = f32x8_or(x2, x0);
	cmp1 = f32x8_or(cmp0, cmp2);
	x2 = f32x8_and(cmp1, x2);
	x = f32x8_andnot(cmp1, x);
	x = f32x8_or(x2, x);

	f32x8_t y = f32x8_or(y0, y1);

	f32x8_t zz = f32x8_mul(x, x);
	f32x8_t acc = kVec8f_atancof_p0;
	acc = f32x8_msub(acc, zz, kVec8f_atancof_p1);
	acc = f32x8_madd(acc, zz, kVec8f_atancof_p2);
	acc = f32x8_msub(acc, zz, kVec8f_atancof_p3);
	acc = f32x8_mul(acc, zz);
	acc = f32x8_madd(acc, x, x);
	y = f32x8_add(y, acc);

	// update the sign
	y = f32x8_xor(y, sign_bit);

	return y;
}
#endif // defined(JX_MATH_SIMD_AVX2)
