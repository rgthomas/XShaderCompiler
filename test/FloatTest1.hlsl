
// HLSL Translator: Floating-point Scanner Test 1
// 05/11/2016


// VERTEX SHADER

#define X

float f1()
{
	return 1.0;
}

float4 VS() : SV_Position
{
	struct S
	{
		float val;
	};
	
	S S_val;
	
	float a = 0.0;
	float b = .0;
	float c = 0.;
	float d = 0.f;
	float e = 1.5e+5;
	float f = 1.5e25;
	float g = 1.5E-10F;
	float h = - - - -.1+-2;
	float i = +-+-+-.1;
	float j = .1h;
	float2 k = 1 .xx;
	float3 l = 1X.xxx;
	float4 m = 1. .xxxx;
	float4 n = 1..xxxx;
	float4 o = 1.0.xxxx;
	float4 p = 1 .xxxx.zz.y.xxxx;
	float4 q = float2(0, 1).y.xxxx;
	float4 r = f1().xxxx;
	float4 s = S_val.val.xxxx;
	
	uint ui_a = 1;
	
	return float4(0, 0, 1, 1).zwzw;
}

