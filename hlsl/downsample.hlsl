// license:BSD-3-Clause
// copyright-holders:Ryan Holtz,ImJezze
//-----------------------------------------------------------------------------
// Downsample Effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
	float2 ScreenDims;
	float2 TargetDims;
	float2 QuadDims;
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
	float4 TexCoord01 : TEXCOORD0;
	float4 TexCoord23 : TEXCOORD1;
};

struct VS_INPUT
{
	float3 Position : POSITION;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
	float2 VecTex : TEXCOORD1;
};

struct PS_INPUT
{
	float4 Color : COLOR0;
	float4 TexCoord01 : TEXCOORD0;
	float4 TexCoord23 : TEXCOORD1;
};

//-----------------------------------------------------------------------------
// Downsample Vertex Shader
//-----------------------------------------------------------------------------

static const float2 Coord0Offset = float2(-0.5f, -0.5f);
static const float2 Coord1Offset = float2( 0.5f, -0.5f);
static const float2 Coord2Offset = float2(-0.5f,  0.5f);
static const float2 Coord3Offset = float2( 0.5f,  0.5f);

VS_OUTPUT vs_main(VS_INPUT Input)
{
	VS_OUTPUT Output = (VS_OUTPUT)0;

	Output.Position = float4(Input.Position.xyz, 1.0f);
	Output.Position.xy /= ScreenDims;
	Output.Position.y = 1.0f - Output.Position.y; // flip y
	Output.Position.xy -= 0.5f; // center
	Output.Position.xy *= 2.0f; // zoom

	Output.Color = Input.Color;

	float2 TexCoord = Input.TexCoord;

	Output.TexCoord01.xy = TexCoord + Coord0Offset;
	Output.TexCoord01.zw = TexCoord + Coord1Offset;
	Output.TexCoord23.xy = TexCoord + Coord2Offset;
	Output.TexCoord23.zw = TexCoord + Coord3Offset;

	return Output;
}

//-----------------------------------------------------------------------------
// Downsample Pixel Shader
//-----------------------------------------------------------------------------

float4 ps_main(PS_INPUT Input) : COLOR
{
	float4 texel0 = tex2D(DiffuseSampler, Input.TexCoord01.xy);
	float4 texel1 = tex2D(DiffuseSampler, Input.TexCoord01.zw);
	float4 texel2 = tex2D(DiffuseSampler, Input.TexCoord23.xy);
	float4 texel3 = tex2D(DiffuseSampler, Input.TexCoord23.zw);

	return (texel0 + texel1 + texel2 + texel3) * 0.25;
}
