//
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

//--------------------------------------------------------------------------------------
// File: SeparableFilter11.hlsl
//
// HLSL source that defines shaders used to render the main scene, and screen sized quads.
//--------------------------------------------------------------------------------------


// CB used by the main scene
cbuffer cbUtility : register( b1 )
{
	float4x4 g_f4x4World;					// World matrix for object
	float4x4 g_f4x4WorldViewProjection;		// World * View * Projection matrix
	float4 g_f4EyePt;						// Eye point	
}


// The input textures
Texture2D g_txInput     : register( t0 ); 
Texture2D g_txDiffuse	: register( t1 );
Texture2D g_txNormal	: register( t2 );


// Samplers
SamplerState g_LinearSampler : register (s0);


// Input vertex structure used by the main scene VS
struct VS_RenderSceneInput
{
    float3 f3Position	: POSITION;  
    float3 f3Normal		: NORMAL;     
    float2 f2TexCoord	: TEXTURE0;
	float3 f3Tangent	: TANGENT;    
};


// Input structure used by the main scene PS
struct PS_RenderSceneInput
{
    float4 f4Position   : SV_Position;
	float2 f2TexCoord	: TEXTURE0;
	float3 f3Normal     : NORMAL; 
	float3 f3Tangent    : TANGENT;
	float3 f3WorldPos	: TEXCOORD2;	
};


// Input vertex structure used by the screen quad VS
struct VS_RenderQuadInput
{
    float3 f3Position : POSITION; 
    float2 f2TexCoord : TEXCOORD0; 
};


// Input structure used by the screen quad PS
struct PS_RenderQuadInput
{
    float4 f4Position : SV_POSITION;              
    float2 f2TexCoord : TEXCOORD0;
};


// Lighting constants
static float4 g_f4Directional1 = float4( 0.992, 1.0, 0.880, 0.0 );
static float4 g_f4Directional2 = float4( 0.595, 0.6, 0.528, 0.0 );
static float4 g_f4Ambient = float4(0.525, 0.474, 0.474, 0.0);
static float3 g_f3LightDir1 = float3( 1.705f, 5.557f, -9.380f );
static float3 g_f3LightDir2 = float3( -5.947f, -5.342f, -5.733f );


//--------------------------------------------------------------------------------------
// VS that prepares the position, tangent and normal for dot3 lighting in the PS 
//--------------------------------------------------------------------------------------
PS_RenderSceneInput VS_RenderScene( VS_RenderSceneInput I )
{
    PS_RenderSceneInput O;
     
    // Transform the position from object space to homogeneous projection space
    O.f4Position = mul( float4( I.f3Position, 1.0f ), g_f4x4WorldViewProjection );
    
    // Transform the normal, tangent and position from object space to world space    
    O.f3WorldPos = mul( I.f3Position, (float3x3)g_f4x4World );
    O.f3Normal  = normalize( mul( I.f3Normal, (float3x3)g_f4x4World ) );
	O.f3Tangent = normalize( mul( I.f3Tangent, (float3x3)g_f4x4World ) );
    
	// Pass through tex coords
	O.f2TexCoord = I.f2TexCoord;
    
    return O;    
}


//--------------------------------------------------------------------------------------
// PS that performs dot3 lighting 
//--------------------------------------------------------------------------------------
float4 PS_RenderScene( PS_RenderSceneInput I ) : SV_TARGET
{
    float4 f4O; 

    float3 LD1 = normalize( mul( g_f3LightDir1, (float3x3)g_f4x4World ) );
	float3 LD2 = normalize( mul( g_f3LightDir2, (float3x3)g_f4x4World ) );
	
    // Sample from diffuse and normal textures
    float4 f4Diffuse = g_txDiffuse.Sample( g_LinearSampler, I.f2TexCoord );
    float fSpecMask = f4Diffuse.a;
    float3 f3Norm = g_txNormal.Sample( g_LinearSampler, I.f2TexCoord ).xyz;
    f3Norm *= 2.0f;
    f3Norm -= float3( 1.0f, 1.0f, 1.0f );
    
    // Setup basis matrix and transform sampled normal
    float3 f3Binorm = normalize( cross( I.f3Normal, I.f3Tangent ) );
    float3x3 f3x3BasisMatrix = float3x3( f3Binorm, I.f3Tangent, I.f3Normal );
    f3Norm = normalize( mul( f3Norm, f3x3BasisMatrix ) );

    // Diffuse lighting
    float4 f4Lighting = saturate( dot( f3Norm, LD1.xyz ) ) * g_f4Directional1;
    f4Lighting += saturate( dot( f3Norm, LD2.xyz ) ) * g_f4Directional2;
    f4Lighting += ( g_f4Ambient );
    
    // Calculate specular power
    float3 f3ViewDir = normalize( g_f4EyePt.xyz - I.f3WorldPos );
    float3 f3HalfAngle = normalize( f3ViewDir + LD1.xyz );
    float4 f4SpecPower1 = pow( saturate( dot( f3HalfAngle, f3Norm ) ), 32 ) * g_f4Directional1;
    f3HalfAngle = normalize( f3ViewDir + LD2.xyz );
    float4 f4SpecPower2 = pow( saturate( dot( f3HalfAngle, f3Norm ) ), 32 ) * g_f4Directional2;
   
    // Final combiner
    f4O.xyz = saturate( f4Lighting.xyz * f4Diffuse.xyz + ( f4SpecPower1.xyz + f4SpecPower2.xyz ) * fSpecMask.xxx );
    f4O.w = 1.0f;

    return f4O; 
}


//--------------------------------------------------------------------------------------
// PS for the sky
//--------------------------------------------------------------------------------------
float4 PS_Sky( PS_RenderSceneInput I ) : SV_Target
{
    float4 f4O;

    // Bog standard textured rendering
    f4O.xyz = g_txDiffuse.Sample( g_LinearSampler, I.f2TexCoord ).xyz;
    f4O.w = 1.0f;

    return f4O;
}


//--------------------------------------------------------------------------------------
// Simple VS for rendering a textured screen sized quad
//--------------------------------------------------------------------------------------
PS_RenderQuadInput VSTexturedScreenQuad( VS_RenderQuadInput I )
{
    PS_RenderQuadInput O;
    
    O.f4Position.x = I.f3Position.x;
    O.f4Position.y = I.f3Position.y;
    O.f4Position.z = I.f3Position.z;
    O.f4Position.w = 1.0f;
    
    O.f2TexCoord = I.f2TexCoord;
    
    return O;    
}


//--------------------------------------------------------------------------------------
// Simple PS for rendering a textured screen sized quad
//--------------------------------------------------------------------------------------
float4 PSTexturedScreenQuad( PS_RenderQuadInput I ) : SV_TARGET
{    
    float4 f4Output = 0.0f;
           
    f4Output = g_txInput.Sample( g_LinearSampler, I.f2TexCoord ).xyzw;
    
    return f4Output.xyzw;
}


//--------------------------------------------------------------------------------------
// EOF
//--------------------------------------------------------------------------------------
