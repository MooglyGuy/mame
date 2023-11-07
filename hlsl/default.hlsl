// license:BSD-3-Clause
// copyright-holders:Ryan Holtz, W. M. Martinez
//-----------------------------------------------------------------------------
// Primary Effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
    float2 ScreenDims;
    float2 TargetDims;
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
// Primary Vertex Shaders
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
// Primary Pixel Shaders
//-----------------------------------------------------------------------------

float4 ps_main(VS_OUTPUT Input) : SV_TARGET
{
	return Diffuse.Sample(DiffuseSampler, Input.TexCoord);
}
