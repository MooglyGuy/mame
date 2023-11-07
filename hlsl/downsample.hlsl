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
	float2 BloomShift;
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

	float2 HalfTargetTexelDims = 0.5f / TargetDims;

	Output.Position = float4(Input.Position.xyz, 1.0f);
	Output.Position.xy /= ScreenDims;
	Output.Position.y = 1.0f - Output.Position.y; // flip y
	Output.Position.xy -= 0.5f; // center
	Output.Position.xy *= 2.0f; // zoom

	Output.Color = Input.Color;

	//Output.TexCoord = Input.TexCoord;
	float2 TexCoord = Input.TexCoord;
	//TexCoord += 0.5f / TargetDims; // half texel offset correction (DX9)

	Output.TexCoord01.xy = TexCoord + Coord0Offset * HalfTargetTexelDims;
	Output.TexCoord01.zw = TexCoord + Coord1Offset * HalfTargetTexelDims;
	Output.TexCoord23.xy = TexCoord + Coord2Offset * HalfTargetTexelDims;
	Output.TexCoord23.zw = TexCoord + Coord3Offset * HalfTargetTexelDims;

	return Output;
}

//-----------------------------------------------------------------------------
// Downsample Pixel Shader
//-----------------------------------------------------------------------------

float4 ps_main(VS_OUTPUT Input) : SV_TARGET
{
	//return Diffuse.Sample(DiffuseSampler, Input.TexCoord);
	float4 texel0 = Diffuse.Sample(DiffuseSampler, Input.TexCoord01.xy);
	float4 texel1 = Diffuse.Sample(DiffuseSampler, Input.TexCoord01.zw);
	float4 texel2 = Diffuse.Sample(DiffuseSampler, Input.TexCoord23.xy);
	float4 texel3 = Diffuse.Sample(DiffuseSampler, Input.TexCoord23.zw);

	return (texel0 + texel1 + texel2 + texel3) * 0.25;
}
