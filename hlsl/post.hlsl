// license:BSD-3-Clause
// copyright-holders:Ryan Holtz,ImJezze
//-----------------------------------------------------------------------------
// Shadowmask Effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Macros
//-----------------------------------------------------------------------------

#define MONOCHROME 1
#define DICHROME 2
#define TRICHROME 3

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
	float2 ScreenDims;
	float2 SourceDims;
	float2 TargetDims;
	float2 TargetScale;
	float2 QuadDims;

	float2 ShadowDims; // size of the shadow texture (extended to power-of-two size)
	float2 ShadowUVOffset;

	float SwapXY;

	float PrepareBloom; // disables some effects for rendering bloom textures
	float VectorScreen;

	float HumBarAlpha;

	float TimeMilliseconds;

	float2 ScreenScale;
	float2 ScreenOffset;

	float3 BackColor;

	float ShadowTileMode; // 0 based on screen (quad) dimension, 1 based on source dimension
	float ShadowAlpha;
	float2 ShadowCount;
	float2 ShadowUV;

	float3 Power;
	float3 Floor;

	float ChromaMode;
	float3 ConversionGain;
}

//-----------------------------------------------------------------------------
// Sampler Definitions
//-----------------------------------------------------------------------------

Texture2D    DiffuseTexture : register(t0);
Texture2D    ShadowTexture : register(t1);
SamplerState DiffuseSampler : register(s0);
SamplerState ShadowSampler : register(s1);

//-----------------------------------------------------------------------------
// Vertex Definitions
//-----------------------------------------------------------------------------

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
	float2 ScreenCoord : TEXCOORD1;
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

static const float PI = 3.1415927;
static const float HalfPI = PI * 0.5;
static const float HumBarDesync = 60.0 / 59.94 - 1.0; // difference between the 59.94 Hz field rate and 60 Hz line frequency (NTSC)

//-----------------------------------------------------------------------------
// Shadowmask Vertex Shader
//-----------------------------------------------------------------------------

VS_OUTPUT vs_main(VS_INPUT Input)
{
	VS_OUTPUT Output = (VS_OUTPUT)0;

	Output.Position = float4(Input.Position.xyz, 1.0);
	Output.Position.xy /= ScreenDims;
	Output.Position.y = 1.0 - Output.Position.y; // flip y
	Output.Position.xy -= 0.5; // center
	Output.Position.xy *= 2.0; // zoom

	Output.TexCoord = Input.TexCoord;
	Output.ScreenCoord = Input.Position.xy / ScreenDims;
	Output.Color = Input.Color;

	return Output;
}

//-----------------------------------------------------------------------------
// Shadowmask Pixel Shader
//-----------------------------------------------------------------------------

float2 GetAdjustedCoords(float2 coord)
{
	// center coordinates
	coord -= 0.5;

	// apply screen scale
	coord *= ScreenScale;

	// un-center coordinates
	coord += 0.5;

	// apply screen offset
	coord += ScreenOffset;

	return coord;
}

float2 GetShadowCoord(float2 TargetCoord, float2 SourceCoord)
{
	// base-target dimensions (without oversampling)
	float2 BaseTargetDims = TargetDims / TargetScale;
	BaseTargetDims = SwapXY > 0.f ? BaseTargetDims.yx : BaseTargetDims.xy;

	float2 canvasCoord = ShadowTileMode == 0 ? TargetCoord + ShadowUVOffset / BaseTargetDims : SourceCoord + ShadowUVOffset / SourceDims;
	float2 canvasTexelDims = ShadowTileMode == 0 ? 1.0 / BaseTargetDims : 1.0 / SourceDims;

	float2 shadowDims = ShadowDims;
	float2 shadowUV = ShadowUV;
	float2 shadowCount = ShadowCount;

	// swap x/y in screen mode (not source mode)
	canvasCoord = ShadowTileMode == 0 && SwapXY > 0.f ? canvasCoord.yx : canvasCoord.xy;

	// swap x/y in screen mode (not source mode)
	shadowCount = ShadowTileMode == 0 && SwapXY > 0.f ? shadowCount.yx : shadowCount.xy;

	float2 shadowTile = canvasTexelDims * shadowCount;
	float2 shadowFrac = frac(canvasCoord / shadowTile);

	// swap x/y in screen mode (not source mode)
	shadowFrac = ShadowTileMode == 0 && SwapXY > 0.f ? shadowFrac.yx : shadowFrac.xy;

	return shadowFrac * shadowUV;
}

float4 ps_main(VS_OUTPUT Input) : SV_TARGET
{
	float2 ScreenCoord = Input.ScreenCoord;
	float2 BaseCoord = GetAdjustedCoords(Input.TexCoord);

	// Color
	float4 BaseColor = DiffuseTexture.Sample(DiffuseSampler, BaseCoord);

	// clip border
	if (BaseCoord.x < 0.0 || BaseCoord.y < 0.0 || BaseCoord.x > 1.0 || BaseCoord.y > 1.0)
	{
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	// Color Compression (may not affect bloom)
	if (PrepareBloom < 1.f)
	{
		// increasing the floor of the signal without affecting the ceiling
		BaseColor.rgb = Floor + (1.0f - Floor) * BaseColor.rgb;
	}

	// Color Power (may affect bloom)
	BaseColor.r = pow(BaseColor.r, Power.r);
	BaseColor.g = pow(BaseColor.g, Power.g);
	BaseColor.b = pow(BaseColor.b, Power.b);

	// Hum Bar Simulation (may not affect vector screen)
	if (PrepareBloom < 1.f && VectorScreen < 1.f && HumBarAlpha > 0.f)
	{
		float HumBarStep = frac(TimeMilliseconds * HumBarDesync);
		float HumBarBrightness = 1.0 - frac(BaseCoord.y + HumBarStep) * HumBarAlpha;
		BaseColor.rgb *= HumBarBrightness;
	}

	// Mask Simulation (may not affect bloom)
	if (PrepareBloom < 1.f && ShadowAlpha > 0.0)
	{
		float2 ShadowCoord = GetShadowCoord(ScreenCoord, BaseCoord);

		float4 ShadowColor = ShadowTexture.Sample(ShadowSampler, ShadowCoord);
		float3 ShadowMaskColor = lerp(1.0, ShadowColor.rgb, ShadowAlpha);
		float ShadowMaskClear = (1.0 - ShadowColor.a) * ShadowAlpha;

		// apply shadow mask color
		BaseColor.rgb *= ShadowMaskColor;
		// clear shadow mask by background color
		BaseColor.rgb = lerp(BaseColor.rgb, BackColor, ShadowMaskClear);
	}

	// Preparation for phosphor color conversion
	if (ChromaMode == MONOCHROME)
	{
		BaseColor.r = dot(ConversionGain, BaseColor.rgb);
		BaseColor.gb = float2(BaseColor.r, BaseColor.r);
	}
	else if (ChromaMode == DICHROME)
	{
		BaseColor.r = dot(ConversionGain.rg, BaseColor.rg);
		BaseColor.g = BaseColor.r;
	}

	return BaseColor;
}
