// license:BSD-3-Clause
// copyright-holders:Ryan Holtz,ImJezze
//-----------------------------------------------------------------------------
// Scanline Effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
	float2 ScreenDims; // 0
	float2 SourceDims; // 8

	float2 TargetDims; // 16
	float2 QuadDims; // 24

	float SwapXY; // 32
	float2 ScreenScale; // 36

	float2 ScreenOffset; // 48

	float ScanlineAlpha; // 56
	float ScanlineScale; // 60
	float ScanlineHeight; // 64
	float ScanlineVariation; // 68
	float ScanlineOffset; // 72
	float ScanlineBrightScale; // 76
	float ScanlineBrightOffset; // 80
}

//-----------------------------------------------------------------------------
// Sampler Definitions
//-----------------------------------------------------------------------------

Texture2D    DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);

//-----------------------------------------------------------------------------
// Vertex Definitions
//-----------------------------------------------------------------------------

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
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

static const float PI = 3.1415927f;
static const float HalfPI = PI * 0.5f;

//-----------------------------------------------------------------------------
// Scanline Vertex Shader
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

	return Output;
}

//-----------------------------------------------------------------------------
// Scanline Pixel Shader
//-----------------------------------------------------------------------------

float2 GetAdjustedCoords(float2 coord)
{
	// center coordinates
	coord -= 0.5f;

	// apply screen scale
	coord *= ScreenScale;

	// un-center coordinates
	coord += 0.5f;

	// apply screen offset
	coord += ScreenOffset;

	return coord;
}

float4 ps_main(VS_OUTPUT Input) : SV_TARGET
{
	// Color
	float4 BaseColor = DiffuseTexture.Sample(DiffuseSampler, Input.TexCoord);

	// clip border
	if (Input.TexCoord.x < 0.0f || Input.TexCoord.y < 0.0f || Input.TexCoord.x > 1.0f || Input.TexCoord.y > 1.0f)
	{
		// return black for the area outside the screen
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	float BrightnessOffset = (ScanlineBrightOffset * ScanlineAlpha);
	float BrightnessScale = (ScanlineBrightScale * ScanlineAlpha) + (1.0f - ScanlineAlpha);

	float ColorBrightness = 0.299f * BaseColor.r + 0.587f * BaseColor.g + 0.114 * BaseColor.b;

	float ScanlineCoord = Input.TexCoord.y;
	ScanlineCoord += SwapXY
		? QuadDims.x <= SourceDims.x * 2.0f
			? 0.5f / QuadDims.x // uncenter scanlines if the quad is less than twice the size of the source
			: 0.0f
		: QuadDims.y <= SourceDims.y * 2.0f
			? 0.5f / QuadDims.y // uncenter scanlines if the quad is less than twice the size of the source
			: 0.0f;
	ScanlineCoord *= SourceDims.y * ScanlineScale * PI;

	float ScanlineCoordJitter = ScanlineOffset * HalfPI;
	float ScanlineSine = sin(ScanlineCoord + ScanlineCoordJitter);
	float ScanlineWide = ScanlineHeight + ScanlineVariation * max(1.0f, ScanlineHeight) * (1.0f - ColorBrightness);
	float ScanlineAmount = pow(ScanlineSine * ScanlineSine, ScanlineWide);
	float ScanlineBrightness = ScanlineAmount * BrightnessScale + BrightnessOffset * BrightnessScale;

	BaseColor.rgb *= lerp(1.0f, ScanlineBrightness, ScanlineAlpha);

	return BaseColor;
}
