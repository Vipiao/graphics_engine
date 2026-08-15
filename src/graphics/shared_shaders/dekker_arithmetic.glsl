// dekker_arithmetic.glsl
//
// A wider float than the hardware has, built out of two of the ones it does: a
// high part holding the value as a float would, and a low part holding exactly
// what the high part could not. Together they carry about 48 bits of mantissa
// against a float's 24, which is enough to place a vertex on a planet to a
// fraction of a millimetre.
//
// The GPU twin of utils/src/math/DekkerArithmetic.h, and must be kept in step
// with it. Every algorithm here is that file's, at the same widths, so a result
// that disagrees between the two is a bug in one of them.
//
// Why this and not double: a consumer GPU runs double at a sixty-fourth of float
// rate, and some have no double hardware at all, while this is float throughout.
//
// The catch is that all of it depends on rounding error surviving to be measured.
// An optimizer that reassociates a + b - a into b, or fuses a multiply and an add
// into one rounding, silently turns any of this back into a plain float -- with
// no error, no warning, and nothing visible until the last bits start to matter.
// That is what `precise` forbids, and why it appears on every accumulation below.
// Products are written as an explicit fma() rather than left to contraction, for
// the same reason from the other direction: the fusing has to happen there.

struct Df {
   float hi;
   float lo;
};

struct Df2 {
   vec2 hi;
   vec2 lo;
};

struct Df3 {
   vec3 hi;
   vec3 lo;
};

Df dfFromFloat(float value) { return Df(value, 0.0); }
Df3 df3FromVec(vec3 value) { return Df3(value, vec3(0.0)); }

// Back to one float. The sum is the value both parts were carrying, rounded once.
float dfToFloat(Df a) { return a.hi + a.lo; }
vec3 df3ToVec(Df3 a) { return a.hi + a.lo; }

// Two-sum. The larger addend leads, so that subtracting the rounded total from it
// recovers what the rounding discarded; taken the other way round the recovery is
// itself inexact. The choice is a select rather than a branch, since neighbouring
// vertices have no reason to agree on which way it goes.
Df dfAdd(Df a, Df b) {
   precise float total = a.hi + b.hi;

   bool aLeads = abs(a.hi) > abs(b.hi);
   precise float big = aLeads ? a.hi : b.hi;
   precise float small = aLeads ? b.hi : a.hi;
   precise float bigLow = aLeads ? a.lo : b.lo;
   precise float smallLow = aLeads ? b.lo : a.lo;

   precise float remainder = big - total + small + smallLow + bigLow;
   precise float hi = total + remainder;

   return Df(hi, total - hi + remainder);
}

// The same algorithm three components wide. GLSL has no templates, so the widths
// are written out; they must stay identical, not merely similar.
Df3 df3Add(Df3 a, Df3 b) {
   precise vec3 total = a.hi + b.hi;

   bvec3 aLeads = greaterThan(abs(a.hi), abs(b.hi));
   precise vec3 big = mix(b.hi, a.hi, aLeads);
   precise vec3 small = mix(a.hi, b.hi, aLeads);
   precise vec3 bigLow = mix(b.lo, a.lo, aLeads);
   precise vec3 smallLow = mix(a.lo, b.lo, aLeads);

   precise vec3 remainder = big - total + small + smallLow + bigLow;
   precise vec3 hi = total + remainder;

   return Df3(hi, total - hi + remainder);
}

Df dfSub(Df a, Df b) { return dfAdd(a, Df(-b.hi, -b.lo)); }
Df3 df3Sub(Df3 a, Df3 b) { return df3Add(a, Df3(-b.hi, -b.lo)); }
Df3 df3AddVec(Df3 a, vec3 b) { return df3Add(a, df3FromVec(b)); }

// Product of the high parts, exactly: fma computes it without rounding, so
// subtracting the rounded product leaves precisely what rounding lost. The three
// remaining cross terms are already small enough to add in plainly.
Df dfMul(Df a, Df b) {
   precise float product = a.hi * b.hi;
   precise float exact = fma(a.hi, b.hi, -product);
   precise float cross = exact + a.lo * b.lo + (a.hi * b.lo + a.lo * b.hi);

   return Df(product, cross);
}

// A vector scaled by one wide scalar, which is every scaling this is asked for:
// a radius, a reciprocal tile size, a normalizing length.
Df3 df3Scale(Df3 a, Df b) {
   precise vec3 product = a.hi * b.hi;
   precise vec3 exact = fma(a.hi, vec3(b.hi), -product);
   precise vec3 cross = exact + a.lo * b.lo + (a.hi * b.lo + a.lo * b.hi);

   return Df3(product, cross);
}

Df2 df2Scale(Df2 a, Df b) {
   precise vec2 product = a.hi * b.hi;
   precise vec2 exact = fma(a.hi, vec2(b.hi), -product);
   precise vec2 cross = exact + a.lo * b.lo + (a.hi * b.lo + a.lo * b.hi);

   return Df2(product, cross);
}

// Long division: divide the high parts, multiply the quotient back out exactly,
// and divide whatever the first division could not express by the divisor again.
Df dfDiv(Df a, Df b) {
   precise float quotient = a.hi / b.hi;
   precise float product = quotient * b.hi;
   precise float exact = fma(quotient, b.hi, -product);

   precise float remainder =
      (a.hi - product - exact + a.lo - quotient * b.lo) / b.hi;
   precise float hi = quotient + remainder;

   return Df(hi, quotient - hi + remainder);
}

// One Newton step off the hardware's float root, which already has half the bits
// wanted. Non-positive input has no root to refine, and returns zero rather than
// a NaN that would spread through the rest of a vertex.
Df dfSqrt(Df a) {
   if (a.hi <= 0.0) return Df(0.0, 0.0);

   precise float root = sqrt(a.hi);
   precise float product = root * root;
   precise float exact = fma(root, root, -product);

   precise float correction = ((a.hi - product - exact + a.lo) * 0.5) / root;
   precise float hi = root + correction;

   return Df(hi, root - hi + correction);
}

Df df3Dot(Df3 a, Df3 b) {
   precise vec3 product = a.hi * b.hi;
   precise vec3 exact = fma(a.hi, b.hi, -product);
   precise vec3 cross = exact + a.lo * b.lo + (a.hi * b.lo + a.lo * b.hi);

   return dfAdd(dfAdd(Df(product.x, cross.x), Df(product.y, cross.y)),
                Df(product.z, cross.z));
}

Df3 df3Normalize(Df3 a) {
   return df3Scale(a, dfDiv(dfFromFloat(1.0), dfSqrt(df3Dot(a, a))));
}

// The fractional part, narrowed. The whole part is removed while the value is
// still wide, which is the only reason a float can hold what is left: a
// coordinate thousands of units from the origin has nothing but whole units to
// spare. Subtracting the floor is exact, so only the low part rounds, and the
// final fold catches the case where it carried the result just outside [0, 1).
float dfFractToFloat(Df a) {
   precise float whole = floor(a.hi);
   precise float part = (a.hi - whole) + a.lo;

   return part - floor(part);
}

vec2 df2FractToVec(Df2 a) {
   precise vec2 whole = floor(a.hi);
   precise vec2 part = (a.hi - whole) + a.lo;

   return part - floor(part);
}
