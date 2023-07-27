// license:BSD-3-Clause
// copyright-holders:Ryan Holtz,ImJezze
//-----------------------------------------------------------------------------
// Bloom Effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------------

cbuffer constants : register(b0)
{
    float2 ScreenDims;
    float2 TargetDims;
    float2 SourceDims;
    float VectorScreen;

	float Level0Weight;
	float Level1Weight;
	float Level2Weight;
	float Level3Weight;
	float Level4Weight;
	float Level5Weight;
	float Level6Weight;
	float Level7Weight;
	float Level8Weight;

	float BloomBlendMode; // 0 brighten, 1 darken
	float BloomScale;
	float3 BloomOverdrive;
}

//-----------------------------------------------------------------------------
// Sampler Definitions
//-----------------------------------------------------------------------------

Texture2D    DiffuseTexture : register(t0);
Texture2D    BloomTextureA : register(t1);
Texture2D    BloomTextureB : register(t2);
Texture2D    BloomTextureC : register(t3);
Texture2D    BloomTextureD : register(t4);
Texture2D    BloomTextureE : register(t5);
Texture2D    BloomTextureF : register(t6);
Texture2D    BloomTextureG : register(t7);
Texture2D    BloomTextureH : register(t8);
SamplerState DiffuseSampler : register(s0);
SamplerState BloomSamplerA : register(s1);
SamplerState BloomSamplerB : register(s2);
SamplerState BloomSamplerC : register(s3);
SamplerState BloomSamplerD : register(s4);
SamplerState BloomSamplerE : register(s5);
SamplerState BloomSamplerF : register(s6);
SamplerState BloomSamplerG : register(s7);
SamplerState BloomSamplerH : register(s8);

// vector screen uses twice -1 as many bloom levels
Texture2D    BloomTextureI : register(t9);
Texture2D    BloomTextureJ : register(t10);
Texture2D    BloomTextureK : register(t11);
Texture2D    BloomTextureL : register(t12);
Texture2D    BloomTextureM : register(t13);
Texture2D    BloomTextureN : register(t14);
Texture2D    BloomTextureO : register(t15);
SamplerState BloomSamplerI : register(s9);
SamplerState BloomSamplerJ : register(s10);
SamplerState BloomSamplerK : register(s11);
SamplerState BloomSamplerL : register(s12);
SamplerState BloomSamplerM : register(s13);
SamplerState BloomSamplerN : register(s14);
SamplerState BloomSamplerO : register(s15);

//-----------------------------------------------------------------------------
// Vertex Definitions
//-----------------------------------------------------------------------------

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
	float2 TexCoord : TEXCOORD0;
	float2 BloomCoord : TEXCOORD1;
};

struct VS_INPUT
{
    float3 Position : POSITION;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD0;
    float3 VecTex   : TEXCOORD1;
};

struct PS_INPUT
{
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
	float2 BloomCoord : TEXCOORD1;
};

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

static const float E = 2.7182817f;
static const float Gelfond = 23.140692f; // e^pi (Gelfond constant)
static const float GelfondSchneider = 2.6651442f; // 2^sqrt(2) (Gelfond-Schneider constant)

//-----------------------------------------------------------------------------
// Funcions
//-----------------------------------------------------------------------------

// www.stackoverflow.com/questions/5149544/can-i-generate-a-random-number-inside-a-pixel-shader/
float random(float2 seed)
{
	// irrationals for pseudo randomness
	float2 i = float2(Gelfond, GelfondSchneider);

	return frac(cos(dot(seed, i)) * 123456.0f);
}

//-----------------------------------------------------------------------------
// Bloom Vertex Shader
//-----------------------------------------------------------------------------

VS_OUTPUT vs_main(VS_INPUT Input)
{
	VS_OUTPUT Output = (VS_OUTPUT)0;

	Output.Position = float4(Input.Position.xyz, 1.0f);
	Output.Position.xy /= ScreenDims;
	Output.Position.y = 1.0f - Output.Position.y; // flip y
	Output.Position.xy -= 0.5f; // center
	Output.Position.xy *= 2.0f; // zoom

	Output.Color = Input.Color;

	Output.TexCoord = Input.TexCoord;
	Output.TexCoord += 0.5f / TargetDims; // half texel offset correction (DX9)

	Output.BloomCoord = Output.TexCoord;
	Output.BloomCoord += 0.5f / SourceDims;

	return Output;
}

//-----------------------------------------------------------------------------
// Bloom Pixel Shader
//-----------------------------------------------------------------------------

float3 GetNoiseFactor(float3 n, float random)
{
	// smaller n become more noisy
	return 1.0f + random * max(0.0f, 0.25f * pow(E, -8 * n));
}

float4 ps_main(PS_INPUT Input) : COLOR
{
	float4 texel = tex2D(DiffuseSampler, Input.TexCoord);

	float3 texelA = tex2D(BloomSamplerA, Input.BloomCoord.xy).rgb;
	float3 texelB = tex2D(BloomSamplerB, Input.BloomCoord.xy).rgb;
	float3 texelC = tex2D(BloomSamplerC, Input.BloomCoord.xy).rgb;
	float3 texelD = tex2D(BloomSamplerD, Input.BloomCoord.xy).rgb;
	float3 texelE = tex2D(BloomSamplerE, Input.BloomCoord.xy).rgb;
	float3 texelF = tex2D(BloomSamplerF, Input.BloomCoord.xy).rgb;
	float3 texelG = tex2D(BloomSamplerG, Input.BloomCoord.xy).rgb;
	float3 texelH = tex2D(BloomSamplerH, Input.BloomCoord.xy).rgb;

	float3 texelI = float3(0.0f, 0.0f, 0.0f);
	float3 texelJ = float3(0.0f, 0.0f, 0.0f);
	float3 texelK = float3(0.0f, 0.0f, 0.0f);
	float3 texelL = float3(0.0f, 0.0f, 0.0f);
	float3 texelM = float3(0.0f, 0.0f, 0.0f);
	float3 texelN = float3(0.0f, 0.0f, 0.0f);
	float3 texelO = float3(0.0f, 0.0f, 0.0f);

	// vector screen uses twice -1 as many bloom levels
	if (VectorScreen > 0.f)
	{
		texelI = tex2D(BloomSamplerI, Input.BloomCoord.xy).rgb;
		texelJ = tex2D(BloomSamplerJ, Input.BloomCoord.xy).rgb;
		texelK = tex2D(BloomSamplerK, Input.BloomCoord.xy).rgb;
		texelL = tex2D(BloomSamplerL, Input.BloomCoord.xy).rgb;
		texelM = tex2D(BloomSamplerM, Input.BloomCoord.xy).rgb;
		texelN = tex2D(BloomSamplerN, Input.BloomCoord.xy).rgb;
		texelO = tex2D(BloomSamplerO, Input.BloomCoord.xy).rgb;
	}

	float3 blend;

	// brighten
	if (BloomBlendMode < 1.f)
	{
		float3 bloom = float3(0.0f, 0.0f, 0.0f);

		texel.rgb *= Level0Weight;

		if (VectorScreen < 1.f)
		{
			bloom += texelA * Level1Weight;
			bloom += texelB * Level2Weight;
			bloom += texelC * Level3Weight;
			bloom += texelD * Level4Weight;
			bloom += texelE * Level5Weight;
			bloom += texelF * Level6Weight;
			bloom += texelG * Level7Weight;
			bloom += texelH * Level8Weight;
		}
		// vector screen uses twice -1 as many bloom levels
		else
		{
			bloom += texelA * (Level1Weight);
			bloom += texelB * (Level1Weight + Level2Weight) * 0.5f;
			bloom += texelC * (Level2Weight);
			bloom += texelD * (Level2Weight + Level3Weight) * 0.5f;
			bloom += texelE * (Level3Weight);
			bloom += texelF * (Level3Weight + Level4Weight) * 0.5f;
			bloom += texelG * (Level4Weight);
			bloom += texelH * (Level4Weight + Level5Weight) * 0.5f;
			bloom += texelI * (Level5Weight);
			bloom += texelJ * (Level5Weight + Level6Weight) * 0.5f;
			bloom += texelK * (Level6Weight);
			bloom += texelL * (Level6Weight + Level7Weight) * 0.5f;
			bloom += texelM * (Level7Weight);
			bloom += texelN * (Level7Weight + Level8Weight) * 0.5f;
			bloom += texelO * (Level8Weight);
		}

		bloom *= BloomScale;

		float3 bloomOverdrive = max(0.0f, texel.rgb + bloom - 1.0f) * BloomOverdrive;

		bloom.r += bloomOverdrive.g * 0.5f;
		bloom.r += bloomOverdrive.b * 0.5f;
		bloom.g += bloomOverdrive.r * 0.5f;
		bloom.g += bloomOverdrive.b * 0.5f;
		bloom.b += bloomOverdrive.r * 0.5f;
		bloom.b += bloomOverdrive.g * 0.5f;

		float2 NoiseCoord = Input.TexCoord;
		float3 NoiseFactor = GetNoiseFactor(bloom, random(NoiseCoord));

		blend = texel.rgb + bloom * NoiseFactor;
	}

	// darken
	else
	{
		texelA = min(texel.rgb, texelA);
		texelB = min(texel.rgb, texelB);
		texelC = min(texel.rgb, texelC);
		texelD = min(texel.rgb, texelD);
		texelE = min(texel.rgb, texelE);
		texelF = min(texel.rgb, texelF);
		texelG = min(texel.rgb, texelG);
		texelH = min(texel.rgb, texelH);

		blend = texel * Level0Weight;
		blend = lerp(blend, texelA, Level1Weight * BloomScale);
		blend = lerp(blend, texelB, Level2Weight * BloomScale);
		blend = lerp(blend, texelC, Level3Weight * BloomScale);
		blend = lerp(blend, texelD, Level4Weight * BloomScale);
		blend = lerp(blend, texelE, Level5Weight * BloomScale);
		blend = lerp(blend, texelF, Level6Weight * BloomScale);
		blend = lerp(blend, texelG, Level7Weight * BloomScale);
		blend = lerp(blend, texelH, Level8Weight * BloomScale);
	}

	return float4(blend, 1.0f);
}
