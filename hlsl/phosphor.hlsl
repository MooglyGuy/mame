// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//-----------------------------------------------------------------------------
// Phosphor Effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
	float2 ScreenDims;
	float2 TargetDims;

	float Passthrough;

	float DeltaTime;
	float3 Phosphor;
}

//-----------------------------------------------------------------------------
// Sampler Definitions
//-----------------------------------------------------------------------------

Texture2D    Diffuse : register(t0);
Texture2D    LastPass : register(t1);
SamplerState DiffuseSampler : register(s0);
SamplerState PreviousSampler : register(s1);

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

//-----------------------------------------------------------------------------
// Phosphor Vertex Shader
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
// Phosphor Pixel Shader
//-----------------------------------------------------------------------------

static const float F = 30.0;

float4 ps_main(VS_OUTPUT Input) : SV_TARGET
{
	float4 CurrY = Diffuse.Sample(DiffuseSampler, Input.TexCoord);
	float3 PrevY = LastPass.Sample(PreviousSampler, Input.PrevCoord).rgb;

	PrevY[0] *= Phosphor[0] == 0.0 ? 0.0 : pow(Phosphor[0], F * DeltaTime);
	PrevY[1] *= Phosphor[1] == 0.0 ? 0.0 : pow(Phosphor[1], F * DeltaTime);
	PrevY[2] *= Phosphor[2] == 0.0 ? 0.0 : pow(Phosphor[2], F * DeltaTime);
	float a = max(PrevY[0], CurrY[0]);
	float b = max(PrevY[1], CurrY[1]);
	float c = max(PrevY[2], CurrY[2]);
	return (Passthrough > 0.f) ? CurrY : float4(a, b, c, CurrY.a);
}
