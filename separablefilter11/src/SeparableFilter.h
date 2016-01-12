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
// File: SeparableFilter.h
//
// SeparableFilter Class definition.
// Handles the setting of inputs and outputs for a user defined filter, and performs the rendering.
//--------------------------------------------------------------------------------------


#pragma once


class SeparableFilter
{
public:

    // Shader type enumeration
    typedef enum _SHADER_TYPE
    {
        SHADER_TYPE_PIXEL,
        SHADER_TYPE_COMPUTE,
        SHADER_TYPE_MAX
    }SHADER_TYPE;

    // LDS precision enumeration
    typedef enum _LDS_PRECISION_TYPE
    {
        LDS_PRECISION_TYPE_8_BIT,
        LDS_PRECISION_TYPE_16_BIT,
        LDS_PRECISION_TYPE_32_BIT,
        LDS_PRECISION_TYPE_MAX
    }LDS_PRECISION_TYPE;

    // Filter precision enumeration
    typedef enum _FILTER_PRECISION_TYPE
    {
        FILTER_PRECISION_TYPE_FULL,
        FILTER_PRECISION_TYPE_APPROXIMATE,
        FILTER_PRECISION_TYPE_MAX
    }FILTER_PRECISION_TYPE;

    // Kernel radius enumeration
    typedef enum _KERNEL_RADIUS_TYPE
    {
        KERNEL_RADIUS_TYPE_2,
        KERNEL_RADIUS_TYPE_4,
        KERNEL_RADIUS_TYPE_6,
        KERNEL_RADIUS_TYPE_8,
        KERNEL_RADIUS_TYPE_10,
        KERNEL_RADIUS_TYPE_12,
        KERNEL_RADIUS_TYPE_14,
        KERNEL_RADIUS_TYPE_16,
        KERNEL_RADIUS_TYPE_18,
        KERNEL_RADIUS_TYPE_20,
        KERNEL_RADIUS_TYPE_22,
        KERNEL_RADIUS_TYPE_24,
        KERNEL_RADIUS_TYPE_26,
        KERNEL_RADIUS_TYPE_28,
        KERNEL_RADIUS_TYPE_30,
        KERNEL_RADIUS_TYPE_32,
        KERNEL_RADIUS_TYPE_MAX
    }KERNEL_RADIUS_TYPE;

    // Constructor / destructor
    SeparableFilter();
    ~SeparableFilter();
    
    // Call if different from back buffer size 
    void SetOutputSize( unsigned int uWidth, unsigned int uHeight ); 
    
    // Sets in order provided
    void SetShaderResourceViews( ID3D11ShaderResourceView** ppHorizInputViews, ID3D11ShaderResourceView** ppVertInputViews, int iNumInputViews );   
    
    // For the Pixel Shader versions
    void SetRenderTargetViews( ID3D11RenderTargetView* pHorizOutput, ID3D11RenderTargetView* pVertOutput ); 
    
    // For the Compute Shader versions
    void SetUnorderedAccessViews( ID3D11UnorderedAccessView* pHorizOutput, ID3D11UnorderedAccessView* pVertOutput ); 
    
    // Likely set the shaders once after creation, though could be every frame
    void SetPixelShaders( ID3D11PixelShader* pHorizShader, ID3D11PixelShader* pVertShader ); 
    void SetComputeShaders( ID3D11ComputeShader* pHorizShader, ID3D11ComputeShader* pVertShader ); 
    
    // Device hook methods
    HRESULT OnCreateDevice( ID3D11Device* pd3dDevice );
    void OnDestroyDevice();
    void OnResizedSwapChain( const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc );
    void OnRender( SHADER_TYPE ShaderType );

private:

    static const unsigned int   m_uRUN_SIZE  = 128;  // Needs to match RUN_LINES in FilterCommon.hlsl  
    static const unsigned int   m_uRUN_LINES = 2;    // Needs to match RUN_SIZE in FilterCommon.hlsl  

    class ScreenQuadVertex
    {
    public:
        DirectX::XMFLOAT3 v3Pos;
        DirectX::XMFLOAT2 v2TexCoord;
    };

    class CommonConstantBuffer
    {
    public:
        float fOutputSize[4]; // ( [0] = Width, [1] = Height, [2] = Inv Width, [3] = Inv Height )
    };

    ID3D11ShaderResourceView**  m_ppHorizInputViews;
    ID3D11ShaderResourceView**  m_ppVertInputViews;
    ID3D11ShaderResourceView**  m_ppNULLInputViews;
    int                         m_iNumInputViews;
    ID3D11RenderTargetView*     m_pRTOutput[2];
    ID3D11RenderTargetView*     m_pRTNULL;
    ID3D11UnorderedAccessView*  m_pUAVOutput[2];
    ID3D11UnorderedAccessView*  m_pUAVNULL;
    SHADER_TYPE                 m_ShaderType;
    ID3D11PixelShader*          m_pPixelShaders[2];
    ID3D11ComputeShader*        m_pComputeShaders[2];
    ID3D11Buffer*	            m_pScreenQuadVertexBuffer;
    ID3D11InputLayout*          m_pScreenInputLayout;
    ID3D11VertexShader*         m_pVSTexturedScreenQuad;
    ID3D11SamplerState*         m_pLinearClampSampler;
    ID3D11SamplerState*         m_pPointSampler;
    CommonConstantBuffer        m_CommonCB;
    ID3D11Buffer*               m_pCommonCB;
};


//--------------------------------------------------------------------------------------
// EOF
//--------------------------------------------------------------------------------------

