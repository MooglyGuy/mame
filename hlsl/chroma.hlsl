// license:BSD-3-Clause
// copyright-holders:W. M. Martinez
//-----------------------------------------------------------------------------
// Phosphor Chromaticity to sRGB Transform Effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
    float2 ScreenDims;
    float2 TargetDims;
	float3 YGain;
	float2 ChromaA;
	float2 ChromaB;
	float2 ChromaC;
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
	float2 PrevCoord : TEXCOORD1;
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
	float2 TexCoord : TEXCOORD0;
	float2 PrevCoord : TEXCOORD1;
};

//-----------------------------------------------------------------------------
// Chroma Vertex Shader
//-----------------------------------------------------------------------------

VS_OUTPUT vs_main(VS_INPUT Input)
{
	VS_OUTPUT Output = (VS_OUTPUT)0.0;

	Output.Position = float4(Input.Position.xyz, 1.0);
	Output.Position.xy /= ScreenDims;
	Output.Position.y = 1.0 - Output.Position.y; // flip y
	Output.Position.xy -= 0.5; // center
	Output.Position.xy *= 2.0; // zoom

	Output.TexCoord = Input.TexCoord;
	Output.PrevCoord = Input.TexCoord;
	Output.Color = Input.Color;

	return Output;
}

//-----------------------------------------------------------------------------
// Chroma Pixel Shader
//-----------------------------------------------------------------------------

static const float3x3 XYZ_TO_sRGB =
{
	 3.2406, -1.5372, -0.4986,
        -0.9689,  1.8758,  0.0415,
	 0.0557, -0.2040,  1.0570
};

float4 ps_main(PS_INPUT Input) : COLOR
{
	const float4 cin = tex2D(DiffuseSampler, Input.TexCoord);
	float4 cout = float4(0.0, 0.0, 0.0, cin.a);
	const float3x2 xy = { ChromaA, ChromaB, ChromaC };

	for (int i = 0; i < 3; ++i)
	{
		const float Y = YGain[i] * cin[i];
		const float X = xy[i].x * (Y / xy[i].y);
		const float Z = (1.0 - xy[i].x - xy[i].y) * (Y / xy[i].y);
		cout.rgb += mul(XYZ_TO_sRGB, float3(X, Y, Z));
	}
	return cout;
}
