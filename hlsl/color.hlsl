// license:BSD-3-Clause
// copyright-holders:Ryan Holtz, W. M. Martinez
//-----------------------------------------------------------------------------
// Color-Convolution Effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Macros
//-----------------------------------------------------------------------------

#define LUT_TEXTURE_WIDTH 4096.0f
#define LUT_SIZE 64.0f
#define LUT_SCALE float2(1.0f / LUT_TEXTURE_WIDTH, 1.0f / LUT_SIZE)

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
	float2 ScreenDims;
	float2 SourceDims;
	float3 PrimTint;
	float3 RedRatios;
	float3 GrnRatios;
	float3 BluRatios;
	float3 Offset;
	float3 Scale;
	float Saturation;
	float LutEnable;
}

//-----------------------------------------------------------------------------
// Sampler Definitions
//-----------------------------------------------------------------------------

Texture2D    Diffuse : register(t0);
Texture2D    LutTexture : register(t1);
SamplerState DiffuseSampler : register(s0);
SamplerState LutSampler : register(s1);

//-----------------------------------------------------------------------------
// Utilities
//-----------------------------------------------------------------------------

float3 apply_lut(float3 color)
{
	// NOTE: Do not change the order of parameters here.
	float3 lutcoord = float3((color.rg * (LUT_SIZE - 1.0f) + 0.5f) *
		LUT_SCALE, color.b * (LUT_SIZE - 1.0f));
	float shift = floor(lutcoord.z);

	lutcoord.x += shift * LUT_SCALE.y;
	color.rgb = lerp(tex2D(LutSampler, lutcoord.xy).rgb, tex2D(LutSampler,
		float2(lutcoord.x + LUT_SCALE.y, lutcoord.y)).rgb,
		lutcoord.z - shift);
	return color;
}

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

struct PS_INPUT
{
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
};

//-----------------------------------------------------------------------------
// Color-Convolution Vertex Shader
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
	Output.Color.rgb *= PrimTint;

	return Output;
}

//-----------------------------------------------------------------------------
// Color-Convolution Pixel Shader
//-----------------------------------------------------------------------------

float4 ps_main(PS_INPUT Input) : COLOR
{
	float4 BaseTexel = tex2D(DiffuseSampler, Input.TexCoord);

	if (LutEnable > 0.f)
		BaseTexel.rgb = apply_lut(BaseTexel.rgb);

	float3 OutRGB = BaseTexel.rgb;

	// RGB Tint & Shift
	float ShiftedRed = dot(OutRGB, RedRatios);
	float ShiftedGrn = dot(OutRGB, GrnRatios);
	float ShiftedBlu = dot(OutRGB, BluRatios);

	// RGB Scale & Offset
	float3 OutTexel = float3(ShiftedRed, ShiftedGrn, ShiftedBlu) * Scale + Offset;

	// Saturation
	float3 Grayscale = float3(0.299f, 0.587f, 0.114f);
	float OutLuma = dot(OutTexel, Grayscale);
	float3 OutChroma = OutTexel - OutLuma;
	float3 Saturated = OutLuma + OutChroma * Saturation;

	return float4(Saturated * Input.Color.rgb, BaseTexel.a);
}
