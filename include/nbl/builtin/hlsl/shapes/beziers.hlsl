// Copyright (C) 2018-2023 - DevSH Graphics Programming Sp. z O.O.
// This file is part of the "Nabla Engine".
// For conditions of distribution and use, see copyright notice in nabla.h

#ifndef _NBL_BUILTIN_HLSL_SHAPES_BEZIERS_INCLUDED_
#define _NBL_BUILTIN_HLSL_SHAPES_BEZIERS_INCLUDED_

#include <nbl/builtin/hlsl/cpp_compat.hlsl>


// TODO [Przemek]: implement it in intrinsics.h
#ifndef __HLSL_VERSION
#include <algorithm>

namespace nbl::hlsl
{

#define NBL_ALIAS_TEMPLATE_FUNCTION(origFunctionName, functionAlias) \
template<typename... Args> \
inline auto functionAlias(Args&&... args) -> decltype(origFunctionName(std::forward<Args>(args)...)) \
{ \
    return origFunctionName(std::forward<Args>(args)...); \
}

NBL_ALIAS_TEMPLATE_FUNCTION(std::min, min);
NBL_ALIAS_TEMPLATE_FUNCTION(std::max, max);

}

#endif



// TODO: Later include from correct hlsl header (numeric_limits.hlsl)
#ifndef nbl_hlsl_FLT_EPSILON
#define	nbl_hlsl_FLT_EPSILON 5.96046447754e-08
#endif

#define SHADER_CRASHING_ASSERT(expr) \
    do { \
        [branch] if (!(expr)) \
          vk::RawBufferStore<uint32_t>(0xdeadbeefBADC0FFbull,0x45u,4u); \
    } while(true)

namespace nbl
{
namespace hlsl
{
namespace shapes
{
    template<typename float_t>
    struct QuadraticBezier
    {
        using float_t2 = vector<float_t, 2>;
        using float_t3 = vector<float_t, 3>;
        using float_t4 = vector<float_t, 4>;
        using float_t2x2 = matrix<float_t, 2, 2>;

        float_t2 P0;
        float_t2 P1;
        float_t2 P2;

        static QuadraticBezier construct(float_t2 P0, float_t2 P1, float_t2 P2)
        {
            QuadraticBezier ret = { P0, P1, P2 };
            return ret;
        }

        float_t2 evaluate(float_t t) NBL_CONST_MEMBER_FUNC
        {
            float_t2 position = 
                P0 * (1.0 - t) * (1.0 - t) 
                 + 2.0 * P1 * (1.0 - t) * t
                 +       P2 * t         * t;

            return position;
        }

        // from shadertoy: https://www.shadertoy.com/view/stfSzS
        float_t4 BezierAABB(NBL_CONST_REF_ARG(QuadraticBezier<float_t>) quadratic)
        {
            float_t2 mi = min(quadratic.P0, quadratic.P2);
            float_t2 ma = max(quadratic.P0, quadratic.P2);
        
            float_t2 a = quadratic.P0 - 2.0 * quadratic.P1 + quadratic.P2;
            float_t2 b = quadratic.P1 - quadratic.P0;
            float_t2 t = -b / a; // solution for linear equation at + b = 0
        
            if (t.x > 0.0 && t.x < 1.0) // x-coord
            {
                float_t q = quadratic.evaluate(t.x).x;
        
                mi.x = min(mi.x, q);
                ma.x = max(ma.x, q);
            }
        
            if (t.y > 0.0 && t.y < 1.0) // y-coord
            {
                float_t q = quadratic.evaluate(t.y).y;
                
                mi.y = min(mi.y, q);
                ma.y = max(ma.y, q);
            }
        
            return float_t4(mi, ma);
        }

        void OBBAligned(float_t thickness, NBL_REF_ARG(float_t2) obbV0, NBL_REF_ARG(float_t2) obbV1, NBL_REF_ARG(float_t2) obbV2, NBL_REF_ARG(float_t2) obbV3)
        {
            // shift curve so 'p0' is at origin (will become zero)
            float_t2 transformedP0 = float_t2(0.0f, 0.0f);
            float_t2 transformedP1 = P1 - P0;
            float_t2 transformedP2 = P2 - P0;
            
            // rotate it around origin so 'p2' is on x-axis
            // 
            // - columns of matrix represents axes of transformed system and we already have one:
            //   normalized vector from origin to p2 represents x-axis of wanted rotated bounding-box
            // - 2nd axis is perpendicular to the 1st one so we just rotate 1st one counter-clockwise
            //   by 90 degrees
            
            const float_t p2Length = length(transformedP2);
            const float_t2 axis = transformedP2 / p2Length; // normalized (unit length)
            const float_t2 translation = P0;
            float_t2x2 rotation;
            
            rotation[0] = float_t2(  axis.x, axis.y );      // column 0 ... x-axis
            rotation[1] = float_t2( -axis.y, axis.x );      // column 1 ... y-axis ... CCW x-axis by 90 degrees
            
            // notes:
            // - rotating 'p0' is pointless as it is "zero" and none rotation will change that
            // - rotating 'p2' will move it to "global" x-axis so its y-coord will be zero and x-coord
            //   will be its distance from origin
            
            //  transformed.P0 = transformed.P0 * rotation;
            //  transformed.P1 = transformed.P1 * rotation;
            //  transformed.P2 = transformed.P2 * rotation;
            
            transformedP1 = mul(rotation, transformedP1);
            transformedP2 = float_t2(p2Length, 0.0);
            
            // compute AABB of curve in local-space
            QuadraticBezier<float_t> quadraticTransformed = QuadraticBezier<float_t>::construct(transformedP0, transformedP1, transformedP2);
            float_t4 aabb = BezierAABB(quadraticTransformed);
            aabb.xy -= thickness;
            aabb.zw += thickness;
            
            // transform AABB back to world-space
            float_t2 center = translation + mul((aabb.xy + aabb.zw) / 2.0f, rotation);
            float_t2 extent = ((aabb.zw - aabb.xy) / 2.0f).xy;
            //float_t center = p0 + rotation * aabb.center;
            //float_t2 extent = aabb.extent;
            
            obbV0 = float_t2(center + mul(extent, rotation));
            obbV1 = float_t2(center + mul(float_t2(extent.x, -extent.y), rotation));
            obbV2 = float_t2(center + mul(-extent, rotation));
            obbV3 = float_t2(center + mul(-float_t2(extent.x, -extent.y), rotation));
        }
    };

    template<typename float_t>
    struct Quadratic
    {
        using scalar_t = float_t;
        using float_t2 = vector<float_t, 2>;
        using float_t3 = vector<float_t, 3>;
        using float_t2x2 = matrix<float_t, 2, 2>;
        
        float_t2 A;
        float_t2 B;
        float_t2 C;
        
        struct AnalyticArcLengthCalculator
        {
            using float_t2 = vector<float_t, 2>;
        
            static AnalyticArcLengthCalculator construct(float_t lenA2, float_t AdotB, float_t a, float_t b, float_t  c, float_t b_over_4a)
            {
                AnalyticArcLengthCalculator ret = { lenA2, AdotB, a, b, c, b_over_4a };
                return ret;
            }
            
            static AnalyticArcLengthCalculator construct(Quadratic<float_t> quadratic)
            {
                AnalyticArcLengthCalculator ret;
                ret.lenA2 = dot(quadratic.A, quadratic.A);
                // sqrt(dot(A,A)) sqrt(dot(B,B)) cos(angle between A and B)
                ret.AdotB = dot(quadratic.A, quadratic.B);
        
                ret.a = 4.0f * ret.lenA2;
                ret.b = 4.0f * ret.AdotB;
                ret.c = dot(quadratic.B, quadratic.B);
        
                ret.b_over_4a = ret.AdotB / ret.a;
                return ret;
            }
            
            // These are precompured values to calculate arc length which eventually is an integral of sqrt(a*x^2+b*x+c)
            float_t lenA2;
            float_t AdotB;
        
            // differential arc-length is sqrt((dx/dt)^2+(dy/dt)^2), so for a Quad Bezier
            // sqrt((2A_x t + B_x)^2+(2A_y t + B_y)^2) == sqrt(4 (A_x^2+A_y^2) t + 4 (A_x B_x+A_y B_y) t + (B_x^2+B_y^2))
            float_t a;
            float_t b; // 2 sqrt(a) sqrt(c) cos(angle between A and B)
            float_t c;

            float_t b_over_4a;
            
            float_t calcArcLen(float_t t)
            {
                float_t lenTan = sqrt(t*(a*t + b) + c);
                float_t retval = t*lenTan;
                float_t sqrt_c = sqrt(c);
                
                // we skip this because when |a| -> we have += 0/0 * 0 here resulting in NaN
                // happens when P0, P1, P2 in a straight line and equally spaced
                if (lenA2 >= exp2(-19.0f)*c)
                {
                    retval *= 0.5f;
                    // implementation might fall apart when beziers folds back on itself and `b = - 2 sqrt(a) sqrt(c)`
                    // https://www.wolframalpha.com/input?i=%28%28-2Sqrt%5Ba%5DSqrt%5Bc%5D%2B2ax%29+Sqrt%5Bc-2Sqrt%5Ba%5DSqrt%5Bc%5Dx%2Bax%5E2%5D+%2B+2+Sqrt%5Ba%5D+c%29%2F%284a%29
                    retval += b_over_4a*(lenTan - sqrt_c);
                    
                    // sin2 multiplied by length of A and B
                    //  |A|^2 * |B|^2 * -(sin(A, B)^2)
                    float_t lenAB2 = lenA2 * c;
                    float_t lenABcos2 = AdotB * AdotB;
                    // because `b` is linearly dependent on `a` this will also ensure `b_over_4a` is not NaN, ergo `a` has a minimum value
                    // " (1.f+exp2(-23))* " is making sure the difference we compute later is large enough to not cancel/truncate to 0 or worse, go negative (which it analytically shouldn't be able to do)
                    if (lenAB2>(1.0f+exp2(-19.0f))*lenABcos2)
                    {
                        float_t det_over_16 = lenAB2 - lenABcos2;
                        float_t sqrt_a = sqrt(a);
                        float_t subTerm0 = (det_over_16*2.0f)/(a/rsqrt(a));
                        
                        float_t subTerm1 = log(b + 2.0f * sqrt_a * sqrt_c);
                        float_t subTerm2 = log(b + 2.0f * a * t + 2.0f * sqrt_a * lenTan);
                        
                        retval -= subTerm0 * (subTerm1 - subTerm2);
                    }
                }

                return retval;
            }
            
            float_t calcArcLenInverse(NBL_CONST_REF_ARG(Quadratic<float_t>) quadratic, float_t min, float_t max, float_t arcLen, float_t accuracyThreshold, float_t hint)
            {
                return calcArcLenInverse_NR(quadratic, min, max, arcLen, accuracyThreshold, hint);
            }

            float_t calcArcLenInverse_NR(NBL_CONST_REF_ARG(Quadratic<float_t>) quadratic, float_t min, float_t max, float_t arcLen, float_t accuracyThreshold, float_t hint)
            {
                float_t xn = hint;

                if (arcLen <= accuracyThreshold)
                    return arcLen;

                const uint32_t iterationThreshold = 32;
                for(uint32_t n = 0; n < iterationThreshold; n++)
                {
                    const float_t arcLenDiffAtParamGuess = calcArcLen(xn) - arcLen;

                    const bool clampToMin = xn <= min;
                    const bool clampToMax = xn >= max;
                    const bool rootIsOutOfBounds = (clampToMin && arcLenDiffAtParamGuess > 0) || (clampToMax && arcLenDiffAtParamGuess < 0);

                    if (abs(arcLenDiffAtParamGuess) < accuracyThreshold || rootIsOutOfBounds)
                        break;

                    float_t differentialAtGuess = length(2.0*quadratic.A * xn + quadratic.B);
                        // x_n+1 = x_n - f(x_n)/f'(x_n)
                    xn -= arcLenDiffAtParamGuess / differentialAtGuess;

                    if (clampToMin)
                        xn = min;
                    else if (clampToMax)
                        xn = max;
                }

                return xn;
            }

            float_t calcArcLenInverse_Bisection(NBL_CONST_REF_ARG(Quadratic<float_t>) quadratic, float_t min, float_t max, float_t arcLen, float_t accuracyThreshold, float_t hint)
            {
                float_t xn = hint;
            
                if (arcLen <= accuracyThreshold)
                    return arcLen;
            
                const uint32_t iterationThreshold = 32;
                for (uint32_t n = 0; n < iterationThreshold; n++)
                {
                    float_t arcLenDiffAtParamGuess = calcArcLen(xn) - arcLen;
            
                    if (abs(arcLenDiffAtParamGuess) < accuracyThreshold)
                        return xn;
            
                    if (arcLenDiffAtParamGuess < 0.0)
                        min = xn;
                    else
                        max = xn;
            
                    xn = (min + max) / 2.0;
                }
            
                return xn;
            }

        };
        
        using ArcLengthCalculator = AnalyticArcLengthCalculator;
        
        static Quadratic construct(NBL_CONST_REF_ARG(float_t2) A, NBL_CONST_REF_ARG(float_t2) B, NBL_CONST_REF_ARG(float_t2) C)
        {            
            Quadratic ret = { A, B, C };
            return ret;
        }

        static Quadratic constructFromBezier(float_t2 P0, float_t2 P1, float_t2 P2)
        {
            const float_t2 A = P0 - 2.0 * P1 + P2;
            const float_t2 B = 2.0 * (P1 - P0);
            const float_t2 C = P0;

            Quadratic ret = { A, B, C };
            return ret;
        }

        static Quadratic constructFromBezier(NBL_CONST_REF_ARG(QuadraticBezier<float_t>) curve)
        {
            return constructFromBezier(curve.P0, curve.P1, curve.P2);
        }

        float_t2 evaluate(float_t t) NBL_CONST_MEMBER_FUNC
        {
            return t * (A * t + B) + C;
        }

        float_t2 derivative(float_t t) NBL_CONST_MEMBER_FUNC
        {
            return float_t(2.0) * A * t + B;
        }

        float_t2 secondDerivative(float_t t) NBL_CONST_MEMBER_FUNC
        {
            return float_t(2.0) * A;
        }

        NBL_CONSTEXPR_STATIC_INLINE uint32_t MaxCandidates = 3u;
        using Candidates = vector<float_t, MaxCandidates>;

        Candidates getClosestCandidates(NBL_CONST_REF_ARG(float_t2) pos)
        {
            // p(t)    = (1-t)^2*A + 2(1-t)t*B + t^2*C
            // p'(t)   = 2*t*(A-2*B+C) + 2*(B-A)
            // p'(0)   = 2(B-A)
            // p'(1)   = 2(C-B)
            // p'(1/2) = 2(C-A)
            
            // exponent so large it would wipe the mantissa on any relative operation
            const float_t PARAMETER_THRESHOLD = exp2(24);
            Candidates candidates;
            
            float_t2 Bdiv2 = B*0.5f;
            float_t2 CsubPos = C - pos;
            float_t Alen2 = dot(A, A);
            
            // if A = 0, solve linear instead
            if(Alen2 < exp2(-23.0f)*dot(B,B))
            {
                candidates[0] = abs(dot(2.0f*Bdiv2,CsubPos))/dot(2.0f*Bdiv2,2.0f*Bdiv2);
                candidates[1] = PARAMETER_THRESHOLD;
                candidates[2] = PARAMETER_THRESHOLD;
            }
            else
            {
                // Reducing Quartic to Cubic Solution
                float_t kk = 1.0 / Alen2;
                float_t kx = kk * dot(Bdiv2,A);
                float_t ky = kk * (2.0*dot(Bdiv2,Bdiv2)+dot(CsubPos,A)) / 3.0;
                float_t kz = kk * dot(CsubPos,Bdiv2);

                // Cardano's Solution to resolvent cubic of the form: y^3 + 3py + q = 0
                // where it was initially of the form x^3 + ax^2 + bx + c = 0 and x was replaced by y - a/3
                // so a/3 needs to be subtracted from the solution to the first form to get the actual solution
                float_t p = ky - kx*kx;
                float_t p3 = p*p*p;
                float_t q = kx*(2.0*kx*kx - 3.0*ky) + kz;
                float_t h = q*q + 4.0*p3;
            
                if(h < 0.0)
                {
                    // 3 roots
                    float_t z = sqrt(-p);
                    float_t v = acos( q/(p*z*2.0) ) / 3.0;
                    float_t m = cos(v);
                    float_t n = sin(v)*1.732050808;
                    
                    candidates[0] = (m + m) * z - kx;
                    candidates[1] = (-n - m) * z - kx;
                    candidates[2] = (n - m) * z - kx;
                }
                else
                {
                    // 1 root
                    h = sqrt(h);
                    float_t2 x = (float_t2(h, -h) - q) / 2.0;

                    // Solving Catastrophic Cancellation when h and q are close (when p is near 0)
                    if(abs(abs(h/q) - 1.0) < 0.0001)
                    {
                       // Approximation of x where h and q are close with no carastrophic cancellation
                       // Derivation (for curious minds) -> h=√(q²+4p³)=q·√(1+4p³/q²)=q·√(1+w)
                          // Now let's do linear taylor expansion of √(1+x) to approximate √(1+w)
                          // Taylor expansion at 0 -> f(x)=f(0)+f'(0)*(x) = 1+½x 
                          // So √(1+w) can be approximated by 1+½w
                          // Simplifying later and the fact that w=4p³/q will result in the following.
                       x = float_t2(p3/q, -q - p3/q);
                    }

                    float_t2 uv = sign(x)*pow(abs(x), float_t2(1.0/3.0,1.0/3.0));
                    candidates[0u] = uv.x + uv.y - kx;
                    candidates[1u] = PARAMETER_THRESHOLD;
                    candidates[2u] = PARAMETER_THRESHOLD;
                }
            }
            
            return candidates;
        }

        float_t2x2 getLocalCoordinateSpace(float_t t)
        {
            // normalize tangent
            float_t2 d = normalize(2 * A * t + B);
            return float_t2x2(d.x, d.y, -d.y, d.x);
        }
    };
}
}
}

#endif