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
// File: BilateralFilter.hlsl
//
// Implements a simple bilateral filter. It uses the scene depth to compute a focal 
// region that is in turn used to determine how blurry a pixel should be. This mimics
// the common DoF effect, but is only intended as an example of how to manipulate the 
// filter macros to perform custom filters.
//--------------------------------------------------------------------------------------

#include "..\\..\\..\\AMD_LIB\\src\\Shaders\\SeparableFilter\\FilterCommon.hlsl"

// Defines
#define PI                      ( 3.1415927f )
#define GAUSSIAN_DEVIATION      ( KERNEL_RADIUS * 0.5f )
#define FOCAL_END               ( 20.0f )
#define FOCAL_END_RAMP          ( 5.0f )

// Constant buffer used by bilateral filters
cbuffer cbBF : register( b2 )
{
    float4 g_f4ProjParams;  //  x = fQTimesZNear, y = fQ
}

// The input textures
Texture2D g_txColor : register( t0 ); 
Texture2D g_txDepth : register( t1 ); 

// The output UAV used by the CS 
RWTexture2D<float4> g_uavOutput : register( u0 );

// CS output structure
struct CS_Output
{
    float4 f4Color[PIXELS_PER_THREAD]; 
};

// PS output structure
struct PS_Output
{
    float4 f4Color[1]; 
};

// Uncompressed data as sampled from inputs
struct RAWDataItem
{
    float3 f3Color;
    float fDepth;
    float fFocal;
};

// Data stored for the kernel
struct KernelData
{
    float fWeight;
    float fWeightSum;
    float fCenterDepth;
    float fCenterFocal;
};


//--------------------------------------------------------------------------------------
// LDS definition and access macros
//--------------------------------------------------------------------------------------
#ifdef USE_COMPUTE_SHADER

    #if( LDS_PRECISION == 32 )

        struct LDS_Layout
        {
            float4  f4Color;
            float   fDepth;
        };
    
        groupshared struct
        {
            LDS_Layout Item[RUN_LINES][RUN_SIZE_PLUS_KERNEL];
        }g_LDS;

        #define WRITE_TO_LDS( _RAWDataItem, _iLineOffset, _iPixelOffset ) \
            g_LDS.Item[_iLineOffset][_iPixelOffset].f4Color = float4( _RAWDataItem.f3Color, _RAWDataItem.fFocal ); \
            g_LDS.Item[_iLineOffset][_iPixelOffset].fDepth = _RAWDataItem.fDepth;
            
        #define READ_FROM_LDS( _iLineOffset, _iPixelOffset, _RAWDataItem ) \
            _RAWDataItem.f3Color = g_LDS.Item[_iLineOffset][_iPixelOffset].f4Color.xyz; \
            _RAWDataItem.fFocal = g_LDS.Item[_iLineOffset][_iPixelOffset].f4Color.w; \
            _RAWDataItem.fDepth = g_LDS.Item[_iLineOffset][_iPixelOffset].fDepth;
            
    #elif( LDS_PRECISION == 16 )

        struct LDS_Layout
        {
            uint2   u2Color;
            float   fDepth;
        };
    
        groupshared struct
        {
            LDS_Layout Item[RUN_LINES][RUN_SIZE_PLUS_KERNEL];
        }g_LDS;

        #define WRITE_TO_LDS( _RAWDataItem, _iLineOffset, _iPixelOffset ) \
            g_LDS.Item[_iLineOffset][_iPixelOffset].u2Color = uint2( Float2ToUint( _RAWDataItem.f3Color.xy ), Float2ToUint( float2( _RAWDataItem.f3Color.z, _RAWDataItem.fFocal ) ) ); \
            g_LDS.Item[_iLineOffset][_iPixelOffset].fDepth = _RAWDataItem.fDepth;
            
        #define READ_FROM_LDS( _iLineOffset, _iPixelOffset, _RAWDataItem ) \
            float2 f2A = UintToFloat2( g_LDS.Item[_iLineOffset][_iPixelOffset].u2Color.x ); \
            float2 f2B = UintToFloat2( g_LDS.Item[_iLineOffset][_iPixelOffset].u2Color.y ); \
            _RAWDataItem.f3Color = float3( f2A, f2B.x ); \
            _RAWDataItem.fFocal = f2B.y; \
            _RAWDataItem.fDepth = g_LDS.Item[_iLineOffset][_iPixelOffset].fDepth;
                        
    #else //( LDS_PRECISION == 8 )

        struct LDS_Layout
        {
            uint    uColor;
            float   fDepth;
        };
    
        groupshared struct
        {
            LDS_Layout Item[RUN_LINES][RUN_SIZE_PLUS_KERNEL];
        }g_LDS;

        #define WRITE_TO_LDS( _RAWDataItem, _iLineOffset, _iPixelOffset ) \
            g_LDS.Item[_iLineOffset][_iPixelOffset].uColor = Float4ToUint( float4( _RAWDataItem.f3Color.xyz, _RAWDataItem.fFocal ) ); \
            g_LDS.Item[_iLineOffset][_iPixelOffset].fDepth = _RAWDataItem.fDepth;
            
        #define READ_FROM_LDS( _iLineOffset, _iPixelOffset, _RAWDataItem ) \
            float4 f4ColorAndFocal = UintToFloat4( g_LDS.Item[_iLineOffset][_iPixelOffset].uColor ); \
            _RAWDataItem.f3Color = f4ColorAndFocal.xyz; \
            _RAWDataItem.fFocal = f4ColorAndFocal.w; \
            _RAWDataItem.fDepth = g_LDS.Item[_iLineOffset][_iPixelOffset].fDepth;

    #endif

#endif


//--------------------------------------------------------------------------------------
// Get a Gaussian weight
//--------------------------------------------------------------------------------------
#define GAUSSIAN_WEIGHT( _fX, _fDeviation, _fWeight ) \
    _fWeight = 1.0f / sqrt( 2.0f * PI * _fDeviation * _fDeviation ); \
    _fWeight *= exp( -( _fX * _fX ) / ( 2.0f * _fDeviation * _fDeviation ) ); 
    

//--------------------------------------------------------------------------------------
// Sample from chosen input(s)
//--------------------------------------------------------------------------------------
#ifdef HORIZ

    #define SAMPLE_FROM_INPUT( _Sampler, _f2SamplePosition, _RAWDataItem ) \
        _RAWDataItem.f3Color = g_txColor.SampleLevel( _Sampler, _f2SamplePosition, 0 ).xyz; \
        _RAWDataItem.fDepth = g_txDepth.SampleLevel( _Sampler, _f2SamplePosition, 0 ).x; \
        _RAWDataItem.fDepth = -g_f4ProjParams.x / ( _RAWDataItem.fDepth - g_f4ProjParams.y ); \
        _RAWDataItem.fFocal = smoothstep( FOCAL_END, FOCAL_END + FOCAL_END_RAMP, _RAWDataItem.fDepth );

#else // VERT

    #define SAMPLE_FROM_INPUT( _Sampler, _f2SamplePosition, _RAWDataItem ) \
        float4 f4Sample = g_txColor.SampleLevel( _Sampler, _f2SamplePosition, 0 ).xyzw; \
        _RAWDataItem.f3Color = f4Sample.xyz; \
        _RAWDataItem.fFocal = f4Sample.w; \
        _RAWDataItem.fDepth = g_txDepth.SampleLevel( _Sampler, _f2SamplePosition, 0 ).x; \
        _RAWDataItem.fDepth = -g_f4ProjParams.x / ( _RAWDataItem.fDepth - g_f4ProjParams.y );
        
#endif


//--------------------------------------------------------------------------------------
// Compute what happens at the kernels center 
//--------------------------------------------------------------------------------------
#define KERNEL_CENTER( _KernelData, _iPixel, _iNumPixels, _O, _RAWDataItem ) \
    [unroll] for( _iPixel = 0; _iPixel < _iNumPixels; ++_iPixel ) { \
        GAUSSIAN_WEIGHT( 0, GAUSSIAN_DEVIATION, _KernelData[_iPixel].fWeight ) \
        _KernelData[_iPixel].fCenterDepth = _RAWDataItem[_iPixel].fDepth; \
        _KernelData[_iPixel].fCenterFocal = _RAWDataItem[_iPixel].fFocal; \
        _KernelData[_iPixel].fWeightSum = _KernelData[_iPixel].fWeight; \
        _O.f4Color[_iPixel].xyz = _RAWDataItem[_iPixel].f3Color * _KernelData[_iPixel].fWeight; }     
        

//--------------------------------------------------------------------------------------
// Compute what happens for each iteration of the kernel 
//--------------------------------------------------------------------------------------
#define KERNEL_ITERATION( _iIteration, _KernelData, _iPixel, _iNumPixels, _O, _RAWDataItem ) \
    [unroll] for( _iPixel = 0; _iPixel < _iNumPixels; ++_iPixel ) { \
        GAUSSIAN_WEIGHT( ( _iIteration - KERNEL_RADIUS + ( 1.0f - 1.0f / float( STEP_SIZE ) ) ), GAUSSIAN_DEVIATION, _KernelData[_iPixel].fWeight ) \
        _KernelData[_iPixel].fWeight *= ( _RAWDataItem[_iPixel].fDepth < _KernelData[_iPixel].fCenterDepth ) ? ( _RAWDataItem[_iPixel].fFocal ) : ( _KernelData[_iPixel].fCenterFocal ); \
        _KernelData[_iPixel].fWeightSum += _KernelData[_iPixel].fWeight; \
        _O.f4Color[_iPixel].xyz += _RAWDataItem[_iPixel].f3Color * _KernelData[_iPixel].fWeight; }
        

//--------------------------------------------------------------------------------------
// Perform final weighting operation 
//--------------------------------------------------------------------------------------
#ifdef HORIZ

    #define KERNEL_FINAL_WEIGHT( _KernelData, _iPixel, _iNumPixels, _O ) \
        [unroll] for( _iPixel = 0; _iPixel < _iNumPixels; ++_iPixel ) { \
            _O.f4Color[_iPixel].w = _KernelData[_iPixel].fCenterFocal; \
            _O.f4Color[_iPixel].xyz /= _KernelData[_iPixel].fWeightSum; }

#else // VERT

    #define KERNEL_FINAL_WEIGHT( _KernelData, _iPixel, _iNumPixels, _O ) \
        [unroll] for( _iPixel = 0; _iPixel < _iNumPixels; ++_iPixel ) { \
            _O.f4Color[_iPixel].w = 1.0f; \
            _O.f4Color[_iPixel].xyz /= _KernelData[_iPixel].fWeightSum; }

#endif
                

//--------------------------------------------------------------------------------------
// Output to chosen UAV 
//--------------------------------------------------------------------------------------
#define KERNEL_OUTPUT( _i2Center, _i2Inc, _iPixel, _iNumPixels, _O, _KernelData ) \
    [unroll] for( _iPixel = 0; _iPixel < _iNumPixels; ++_iPixel ) \
        g_uavOutput[_i2Center + _iPixel * _i2Inc] = _O.f4Color[_iPixel];


//--------------------------------------------------------------------------------------
// Include the filter kernel logic that uses the above macros
//--------------------------------------------------------------------------------------
#include "..\\..\\..\\AMD_LIB\\src\\Shaders\\SeparableFilter\\FilterKernel.hlsl"
#include "..\\..\\..\\AMD_LIB\\src\\Shaders\\SeparableFilter\\HorizontalFilter.hlsl"
#include "..\\..\\..\\AMD_LIB\\src\\Shaders\\SeparableFilter\\VerticalFilter.hlsl"


//--------------------------------------------------------------------------------------
// EOF
//--------------------------------------------------------------------------------------
