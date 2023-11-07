// license:BSD-3-Clause
// copyright-holders:ImJezze
//-----------------------------------------------------------------------------
// Distortion Effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
	float2 ScreenDims; // size of the window or fullscreen
	float ScreenCount;
	float2 TargetDims; // size of the target surface
	float2 TargetScale;
	float2 QuadDims; // size of the screen quad

	float DistortionAmount;      // k     - quartic distortion coefficient
	float CubicDistortionAmount; // kcube - cubic distortion modifier
	float DistortCornerAmount;
	float RoundCornerAmount;
	float SmoothBorderAmount;
	float VignettingAmount;
	float ReflectionAmount;
	float SwapXY;
}

//-----------------------------------------------------------------------------
// Sampler Definitions
//-----------------------------------------------------------------------------

Texture2D    Diffuse : register(t0);
SamplerState DiffuseSampler : register(s0);

//-----------------------------------------------------------------------------
// Vertex Definitions
//-----------------------------------------------------------------------------

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
};

struct VS_INPUT
{
	float3 Position : POSITION;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
	float2 VecTex : TEXCOORD1;
};

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

static const float Epsilon = 1.0e-7f;
static const float PI = 3.1415927f;
static const float E = 2.7182817f;
static const float Gelfond = 23.140692f; // e^pi (Gelfond constant)
static const float GelfondSchneider = 2.6651442f; // 2^sqrt(2) (Gelfond-Schneider constant)
static const float3 LightReflectionColor = float3(1.0f, 0.90f, 0.80f); // color temperature 5.000 Kelvin

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------

// www.stackoverflow.com/questions/5149544/can-i-generate-a-random-number-inside-a-pixel-shader/
float random(float2 seed)
{
	// irrationals for pseudo randomness
	float2 i = float2(Gelfond, GelfondSchneider);

	return frac(cos(dot(seed, i)) * 123456.0f);
}

// www.dinodini.wordpress.com/2010/04/05/normalized-tunable-sigmoid-functions/
float normalizedSigmoid(float n, float k)
{
	// valid for n and k in range of -1.0 and 1.0
	return (n - n * k) / (k - abs(n) * 2.0f * k + 1);
}

// www.iquilezles.org/www/articles/distfunctions/distfunctions.htm
float roundBox(float2 p, float2 b, float r)
{
	return length(max(abs(p) - b + r, 0.0f)) - r;
}

//-----------------------------------------------------------------------------
// Distortion Vertex Shader
//-----------------------------------------------------------------------------

VS_OUTPUT vs_main(VS_INPUT Input)
{
	VS_OUTPUT Output = (VS_OUTPUT)0;

	Output.Position = float4(Input.Position.xyz, 1.0f);
	Output.Position.xy /= ScreenDims;
	Output.Position.y = 1.0f - Output.Position.y; // flip y
	Output.Position.xy -= 0.5f; // center
	Output.Position.xy *= 2.0f; // zoom

	Output.TexCoord = Input.TexCoord;
	Output.Color = Input.Color;

	return Output;
}

//-----------------------------------------------------------------------------
// Distortion Pixel Shader
//-----------------------------------------------------------------------------

float GetNoiseFactor(float3 n, float random)
{
	// smaller n become more noisy
	return 1.0f + random * max(0.0f, 0.25f * pow(E, -8 * n));
}

float GetVignetteFactor(float2 coord, float amount)
{
	float2 VignetteCoord = coord;

	float VignetteLength = length(VignetteCoord);
	float VignetteBlur = (amount * 0.75f) + 0.25;

	// 0.5 full screen fitting circle
	float VignetteRadius = 1.0f - (amount * 0.25f);
	float Vignette = smoothstep(VignetteRadius, VignetteRadius - VignetteBlur, VignetteLength);

	return saturate(Vignette);
}

float GetSpotAddend(float2 coord, float amount)
{
	float2 SpotCoord = coord;

	// upper right quadrant
	float2 spotOffset = float2(-0.25f, 0.25f);

	// normalized screen canvas ratio
	float2 CanvasRatio = SwapXY > 0.f ? float2(1.0f, QuadDims.x / QuadDims.y) : float2(1.0f, QuadDims.y / QuadDims.x);

	SpotCoord += spotOffset;
	SpotCoord *= CanvasRatio;

	float SpotBlur = amount;

	// 0.5 full screen fitting circle
	float SpotRadius = amount * 0.75f;
	float Spot = smoothstep(SpotRadius, SpotRadius - SpotBlur, length(SpotCoord));

	float SigmoidSpot = amount * normalizedSigmoid(Spot, 0.75);

	// increase strength by 100%
	SigmoidSpot = SigmoidSpot * 2.0f;

	return saturate(SigmoidSpot);
}

float GetBoundsFactor(float2 coord, float2 bounds, float radiusAmount, float smoothAmount)
{
	// reduce smooth amount down to radius amount
	smoothAmount = min(smoothAmount, radiusAmount);

	float range = min(bounds.x, bounds.y);
	float amountMinimum = 1.0f / range;
	float radius = range * max(radiusAmount, amountMinimum);
	float smooth = 1.0f / (range * max(smoothAmount, amountMinimum * 2.0f));

	// compute box
	float box = roundBox(bounds * (coord * 2.0f), bounds, radius);

	// apply smooth
	box *= smooth;
	box += 1.0f - pow(smooth * 0.5f, 0.5f);

	float border = smoothstep(1.0f, 0.0f, box);

	return saturate(border);
}

// www.francois-tarlier.com/blog/cubic-lens-distortion-shader/
float2 GetDistortedCoords(float2 centerCoord, float amount, float amountCube)
{
	// lens distortion coefficient
	float k = amount;

	// cubic distortion value
	float kcube = amountCube;

	// compute cubic distortion factor
	float r2 = centerCoord.x * centerCoord.x + centerCoord.y * centerCoord.y;
	float f = kcube == 0.0f ? 1.0f + r2 * k : 1.0f + r2 * (k + kcube * sqrt(r2));

   	// fit screen bounds
	f /= 1.0f + amount * 0.25f + amountCube * 0.125f;

	// apply cubic distortion factor
   	centerCoord *= f;

	return centerCoord;
}

float2 GetTextureCoords(float2 coord, float distortionAmount, float cubicDistortionAmount)
{
	// center coordinates
	coord -= 0.5f;

	// distort coordinates
	coord = GetDistortedCoords(coord, distortionAmount, cubicDistortionAmount);

	// un-center coordinates
	coord += 0.5f;

	return coord;
}

float2 GetQuadCoords(float2 coord, float2 scale, float distortionAmount, float cubicDistortionAmount)
{
	// center coordinates
	coord -= 0.5f;

	// apply scale
	coord *= scale;

	// distort coordinates
	coord = GetDistortedCoords(coord, distortionAmount, cubicDistortionAmount);

	return coord;
}

float4 ps_main(VS_OUTPUT Input) : SV_TARGET
{
	// image distortion
	float distortionAmount = DistortionAmount;
	float cubicDistortionAmount = CubicDistortionAmount > 0.0f
		? CubicDistortionAmount * 1.1f  // cubic distortion need to be a little higher to compensate the quartic distortion
		: CubicDistortionAmount * 1.2f; // negativ values even more

	// corner distortion at least by the amount of the image distorition
	float distortCornerAmount = max(DistortCornerAmount, DistortionAmount + CubicDistortionAmount);

	float roundCornerAmount = RoundCornerAmount * 0.5f;
	float smoothBorderAmount = SmoothBorderAmount * 0.5f;

	float2 TexelDims = 1.0f / TargetDims;

	// base-target dimensions (without oversampling)
	float2 BaseTargetDims = TargetDims / TargetScale;
	BaseTargetDims = SwapXY > 0.f ? BaseTargetDims.yx : BaseTargetDims.xy;

	// base-target/quad difference scale
	float2 BaseTargetQuadScale = ScreenCount == 1
		? BaseTargetDims / QuadDims // keeps the coords inside of the quad bounds of a single screen
		: 1.0f;

	// Screen Texture Curvature
	float2 BaseCoord = GetTextureCoords(Input.TexCoord, distortionAmount, cubicDistortionAmount);

	// Screen Quad Curvature
	float2 QuadCoord = GetQuadCoords(Input.TexCoord, BaseTargetQuadScale, distortCornerAmount, 0.0f);

	// clip border
	if (BaseCoord.x < 0.0f - TexelDims.x || BaseCoord.y < 0.0f - TexelDims.y ||
		BaseCoord.x > 1.0f + TexelDims.x || BaseCoord.y > 1.0f + TexelDims.y)
	{
		return float4(0, 0, 0, 1);
	}

	// Color
	float4 BaseColor = Diffuse.Sample(DiffuseSampler, BaseCoord);

	// Vignetting Simulation
	float2 VignetteCoord = QuadCoord;

	float VignetteFactor = GetVignetteFactor(VignetteCoord, VignettingAmount);
	BaseColor.rgb *= VignetteFactor;

	// Light Reflection Simulation
	float2 SpotCoord = QuadCoord;

	float SpotAddend = GetSpotAddend(SpotCoord, ReflectionAmount) * LightReflectionColor;
	BaseColor.rgb += SpotAddend * GetNoiseFactor(SpotAddend, random(SpotCoord));

	// Round Corners Simulation
	float2 RoundCornerCoord = QuadCoord;
	float2 RoundCornerBounds = ScreenCount == 1
		? QuadDims // align corners to quad bounds of a single screen
		: BaseTargetDims; // align corners to target bounds of multiple screens
	RoundCornerBounds = SwapXY > 0.f ? RoundCornerBounds.yx : RoundCornerBounds.xy;

	float roundCornerFactor = GetBoundsFactor(RoundCornerCoord, RoundCornerBounds, roundCornerAmount, smoothBorderAmount);
	BaseColor.rgb *= roundCornerFactor;

	return BaseColor;
}
