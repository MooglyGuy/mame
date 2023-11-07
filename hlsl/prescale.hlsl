// license:BSD-3-Clause
// copyright-holders:Ryan Holtz,Themaister,ImJezze
//-----------------------------------------------------------------------------
// Pre-scale Effect
//
// Uses the hardware bilinear interpolator to avoid having to sample 4 times manually.
//
// https://github.com/libretro/common-shaders/blob/master/retro/shaders/sharp-bilinear.cg
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
	float2 ScreenDims;
	float2 TargetDims;
	float2 SourceDims;
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
// Pre-scale Vertex Shader
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
// Pre-scale Pixel Shader
//-----------------------------------------------------------------------------

float4 ps_main(VS_OUTPUT Input) : SV_TARGET
{
	float2 Scale = TargetDims / SourceDims;

	float2 TexelDims = Input.TexCoord * SourceDims;
	float2 i = floor(TexelDims);
	float2 s = frac(TexelDims);

	// Figure out where in the texel to sample to get the correct pre-scaled bilinear.
	float2 CenterDistance = s - 0.5f;
	float2 RegionRange = 0.5f - 0.5f / Scale;
	float2 f = (CenterDistance - clamp(CenterDistance, -RegionRange, RegionRange)) * Scale + 0.5f;

	float2 TexCoord = (i + f) / SourceDims;

	return DiffuseTexture.Sample(DiffuseSampler, TexCoord);
}
