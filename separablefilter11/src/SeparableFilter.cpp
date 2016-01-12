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
// File: SeparableFilter.cpp
//
// Implements the SeparableFilter class.
// Handles the setting of inputs and outputs for a user defined filter, and performs the rendering.
//--------------------------------------------------------------------------------------


#include "..\\..\\DXUT\\Core\\DXUT.h"
#include "..\\..\\AMD_SDK\\inc\\AMD_SDK.h"
#include "SeparableFilter.h"


using namespace DirectX;

//--------------------------------------------------------------------------------------
// Constructor
//--------------------------------------------------------------------------------------
SeparableFilter::SeparableFilter()
{
    m_ppHorizInputViews = NULL;
    m_ppVertInputViews = NULL;
    m_ppNULLInputViews = NULL;
    m_iNumInputViews = 0;
    m_pRTOutput[0] = NULL; m_pRTOutput[1] = NULL;
    m_pRTNULL = NULL;
    m_pUAVOutput[0] = NULL; m_pUAVOutput[1] = NULL;
    m_pUAVNULL = NULL;
    m_pPixelShaders[0] = NULL; m_pPixelShaders[1] = NULL;
    m_pComputeShaders[0] = NULL; m_pComputeShaders[1] = NULL;
    m_pScreenQuadVertexBuffer = NULL;
    m_pScreenInputLayout = NULL;
    m_pVSTexturedScreenQuad = NULL;
    m_pLinearClampSampler = NULL;
    m_pPointSampler = NULL;
    memset( &m_CommonCB, 0, sizeof( CommonConstantBuffer ) );
    m_pCommonCB = NULL;
}


//--------------------------------------------------------------------------------------
// destructor
//--------------------------------------------------------------------------------------
SeparableFilter::~SeparableFilter()
{
    m_iNumInputViews = 0;
    SAFE_DELETE( m_ppHorizInputViews );
    SAFE_DELETE( m_ppVertInputViews );
    SAFE_DELETE( m_ppNULLInputViews );
    m_pRTOutput[0] = NULL; m_pRTOutput[1] = NULL;
    m_pUAVOutput[0] = NULL; m_pUAVOutput[1] = NULL;
    m_pPixelShaders[0] = NULL; m_pPixelShaders[1] = NULL;
    m_pComputeShaders[0] = NULL; m_pComputeShaders[1] = NULL;
    SAFE_RELEASE( m_pScreenQuadVertexBuffer );
    SAFE_RELEASE( m_pScreenInputLayout );
    SAFE_RELEASE( m_pVSTexturedScreenQuad );
    SAFE_RELEASE( m_pLinearClampSampler );
    SAFE_RELEASE( m_pPointSampler );
    SAFE_RELEASE( m_pCommonCB );
}


//--------------------------------------------------------------------------------------
// Call this method directly if the intended output size is different from that of
// the main back buffer size 
//--------------------------------------------------------------------------------------
void SeparableFilter::SetOutputSize( unsigned int uWidth, unsigned int uHeight )
{
    m_CommonCB.fOutputSize[0] = (float)uWidth;
    m_CommonCB.fOutputSize[1] = (float)uHeight;
    m_CommonCB.fOutputSize[2] = 1.0f / uWidth;
    m_CommonCB.fOutputSize[3] = 1.0f / uHeight;

    D3D11_MAPPED_SUBRESOURCE MappedResource;
    DXUTGetD3D11DeviceContext()->Map( m_pCommonCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource );
    CommonConstantBuffer* pCommonCB = ( CommonConstantBuffer* )MappedResource.pData;
    pCommonCB->fOutputSize[0] = (float)uWidth; 
    pCommonCB->fOutputSize[1] = (float)uHeight;
    pCommonCB->fOutputSize[2] = 1.0f / (float)uWidth;
    pCommonCB->fOutputSize[3] = 1.0f / (float)uHeight;
    DXUTGetD3D11DeviceContext()->Unmap( m_pCommonCB, 0 );
}


//--------------------------------------------------------------------------------------
// Sets inputs in order provided (base 0)
//--------------------------------------------------------------------------------------
void SeparableFilter::SetShaderResourceViews( ID3D11ShaderResourceView** ppHorizInputViews, ID3D11ShaderResourceView** ppVertInputViews, int iNumInputViews )
{
    assert( NULL != ppHorizInputViews );
    assert( NULL != ppVertInputViews );

    if( iNumInputViews != m_iNumInputViews )
    {
        m_iNumInputViews = iNumInputViews;
        
        SAFE_DELETE( m_ppHorizInputViews );
        m_ppHorizInputViews = new ID3D11ShaderResourceView*[m_iNumInputViews];
        assert( NULL != m_ppHorizInputViews );

        SAFE_DELETE( m_ppVertInputViews );
        m_ppVertInputViews = new ID3D11ShaderResourceView*[m_iNumInputViews];
        assert( NULL != m_ppVertInputViews );

        SAFE_DELETE( m_ppNULLInputViews );
        m_ppNULLInputViews = new ID3D11ShaderResourceView*[m_iNumInputViews];
        assert( NULL != m_ppNULLInputViews );
    }

    for( int iView = 0; iView < m_iNumInputViews; iView++ )
    {
        m_ppHorizInputViews[iView] = ppHorizInputViews[iView];
        m_ppVertInputViews[iView] = ppVertInputViews[iView];
        m_ppNULLInputViews[iView] = NULL;
    }
}


//--------------------------------------------------------------------------------------
// For the Pixel Shader versions
//--------------------------------------------------------------------------------------
void SeparableFilter::SetRenderTargetViews( ID3D11RenderTargetView* pHorizOutput, ID3D11RenderTargetView* pVertOutput )
{
    assert( NULL != pHorizOutput );
    assert( NULL != pVertOutput );

    m_pRTOutput[0] = pHorizOutput;
    m_pRTOutput[1] = pVertOutput;
}


//--------------------------------------------------------------------------------------
// For the Compute Shader versions
//--------------------------------------------------------------------------------------
void SeparableFilter::SetUnorderedAccessViews( ID3D11UnorderedAccessView* pHorizOutput, ID3D11UnorderedAccessView* pVertOutput )
{
    assert( NULL != pHorizOutput );
    assert( NULL != pVertOutput );

    m_pUAVOutput[0] = pHorizOutput;
    m_pUAVOutput[1] = pVertOutput;
}


//--------------------------------------------------------------------------------------
// Likely set the shaders once after creation, though could be every frame
//--------------------------------------------------------------------------------------
void SeparableFilter::SetPixelShaders( ID3D11PixelShader* pHorizShader, ID3D11PixelShader* pVertShader )
{
    assert( NULL != pHorizShader );
    assert( NULL != pVertShader );

    m_pPixelShaders[0] = pHorizShader;
    m_pPixelShaders[1] = pVertShader;
}


//--------------------------------------------------------------------------------------
// Likely set the shaders once after creation, though could be every frame
//--------------------------------------------------------------------------------------
void SeparableFilter::SetComputeShaders( ID3D11ComputeShader* pHorizShader, ID3D11ComputeShader* pVertShader )
{
    assert( NULL != pHorizShader );
    assert( NULL != pVertShader );

    m_pComputeShaders[0] = pHorizShader;
    m_pComputeShaders[1] = pVertShader;
}


//--------------------------------------------------------------------------------------
// Device hook method
//--------------------------------------------------------------------------------------
HRESULT SeparableFilter::OnCreateDevice( ID3D11Device* pd3dDevice )
{
    HRESULT hr = E_FAIL;

    // Linear Clamp sampler
    D3D11_SAMPLER_DESC samDesc;
    ZeroMemory( &samDesc, sizeof(samDesc) );
    samDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samDesc.AddressU = samDesc.AddressV = samDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samDesc.MaxAnisotropy = 1;
    samDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samDesc.MaxLOD = D3D11_FLOAT32_MAX;
    V_RETURN( pd3dDevice->CreateSamplerState( &samDesc, &m_pLinearClampSampler ) );
    // Point sampler
	samDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    V_RETURN( pd3dDevice->CreateSamplerState( &samDesc, &m_pPointSampler ) );

    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc;
    ZeroMemory( &cbDesc, sizeof(cbDesc) );
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof( CommonConstantBuffer );
    V_RETURN( pd3dDevice->CreateBuffer( &cbDesc, NULL, &m_pCommonCB ) );

    // Fill out a unit quad
    ScreenQuadVertex QuadVertices[6];
    QuadVertices[0].v3Pos = XMFLOAT3( -1.0f, -1.0f, 0.5f );
    QuadVertices[0].v2TexCoord = XMFLOAT2( 0.0f, 1.0f );
    QuadVertices[1].v3Pos = XMFLOAT3( -1.0f, 1.0f, 0.5f );
    QuadVertices[1].v2TexCoord = XMFLOAT2( 0.0f, 0.0f );
    QuadVertices[2].v3Pos = XMFLOAT3( 1.0f, -1.0f, 0.5f );
    QuadVertices[2].v2TexCoord = XMFLOAT2( 1.0f, 1.0f );
    QuadVertices[3].v3Pos = XMFLOAT3( -1.0f, 1.0f, 0.5f );
    QuadVertices[3].v2TexCoord = XMFLOAT2( 0.0f, 0.0f );
    QuadVertices[4].v3Pos = XMFLOAT3( 1.0f, 1.0f, 0.5f );
    QuadVertices[4].v2TexCoord = XMFLOAT2( 1.0f, 0.0f );
    QuadVertices[5].v3Pos = XMFLOAT3( 1.0f, -1.0f, 0.5f );
    QuadVertices[5].v2TexCoord = XMFLOAT2( 1.0f, 1.0f );

    // Create the vertex buffer
    D3D11_BUFFER_DESC BD;
    BD.Usage = D3D11_USAGE_DYNAMIC;
    BD.ByteWidth = sizeof( ScreenQuadVertex ) * 6;
    BD.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    BD.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    BD.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    InitData.pSysMem = QuadVertices;
    V_RETURN( pd3dDevice->CreateBuffer( &BD, &InitData, &m_pScreenQuadVertexBuffer ) )

    // Input layout and VS for Screen quads
    ID3DBlob* pBlob = NULL;
    V_RETURN( AMD::CompileShaderFromFile( L"..\\src\\Shaders\\SeparableFilter11.hlsl", "VSTexturedScreenQuad", "vs_5_0", &pBlob, NULL ) ); 
    V_RETURN( pd3dDevice->CreateVertexShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &m_pVSTexturedScreenQuad ) );
	const D3D11_INPUT_ELEMENT_DESC Layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
	V_RETURN( pd3dDevice->CreateInputLayout( Layout, ARRAYSIZE( Layout ), pBlob->GetBufferPointer(), pBlob->GetBufferSize(), &m_pScreenInputLayout ) );
	SAFE_RELEASE( pBlob );

    return hr;
}


//--------------------------------------------------------------------------------------
// Device hook method
//--------------------------------------------------------------------------------------
void SeparableFilter::OnDestroyDevice()
{
    SAFE_RELEASE( m_pLinearClampSampler );
    SAFE_RELEASE( m_pPointSampler );
    SAFE_RELEASE( m_pCommonCB )
    SAFE_RELEASE( m_pScreenQuadVertexBuffer );
    SAFE_RELEASE( m_pVSTexturedScreenQuad );
    SAFE_RELEASE( m_pScreenInputLayout );
}


//--------------------------------------------------------------------------------------
// Device hook method
// If you wish for the output size to be linked to the main backbuffer size, then add this 
// hook to your app.
//--------------------------------------------------------------------------------------
void SeparableFilter::OnResizedSwapChain( const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc )
{
    SetOutputSize( pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height );
}


//--------------------------------------------------------------------------------------
// Device hook method
// Must specify the shader type used
//--------------------------------------------------------------------------------------
void SeparableFilter::OnRender( SHADER_TYPE ShaderType )
{
    // Store the currently set render target
    ID3D11RenderTargetView* pOrigRTV = NULL;
    DXUTGetD3D11DeviceContext()->OMGetRenderTargets( 1, &pOrigRTV, NULL );

    if( ShaderType == SHADER_TYPE_COMPUTE )
    {
        DXUTGetD3D11DeviceContext()->CSSetSamplers( 0, 1, &m_pPointSampler );
        DXUTGetD3D11DeviceContext()->CSSetSamplers( 1, 1, &m_pLinearClampSampler );
        DXUTGetD3D11DeviceContext()->CSSetConstantBuffers( 0, 1, &m_pCommonCB );

        UINT uX, uY, uZ = 1;

        TIMER_Begin( 0, L"Horizontal Pass" )
        
        // CS Horizontal filter pass
        DXUTGetD3D11DeviceContext()->CSSetUnorderedAccessViews( 0, 1, &m_pUAVOutput[0], NULL );
        DXUTGetD3D11DeviceContext()->CSSetShaderResources( 0, m_iNumInputViews, m_ppHorizInputViews );
        DXUTGetD3D11DeviceContext()->CSSetShader( m_pComputeShaders[0], NULL, 0 );
        uX = (int)ceil( (float)m_CommonCB.fOutputSize[0] / m_uRUN_SIZE );
        uY = (int)ceil( (float)m_CommonCB.fOutputSize[1] / m_uRUN_LINES );
        DXUTGetD3D11DeviceContext()->Dispatch( uX, uY, uZ );
        DXUTGetD3D11DeviceContext()->CSSetUnorderedAccessViews( 0, 1, &m_pUAVNULL, NULL );
        DXUTGetD3D11DeviceContext()->CSSetShaderResources( 0, m_iNumInputViews, m_ppNULLInputViews );
        
        TIMER_End() // Horizontal Pass
        
        TIMER_Begin( 0, L"Vertical Pass" )
        
        // CS Vertical filter pass
        DXUTGetD3D11DeviceContext()->CSSetUnorderedAccessViews( 0, 1, &m_pUAVOutput[1], NULL );
        DXUTGetD3D11DeviceContext()->CSSetShaderResources( 0, m_iNumInputViews, m_ppVertInputViews );
        DXUTGetD3D11DeviceContext()->CSSetShader( m_pComputeShaders[1], NULL, 0 );
        uX = (int)ceil( (float)m_CommonCB.fOutputSize[0] / m_uRUN_LINES );
        uY = (int)ceil( (float)m_CommonCB.fOutputSize[1] / m_uRUN_SIZE );
        DXUTGetD3D11DeviceContext()->Dispatch( uX, uY, uZ );
        DXUTGetD3D11DeviceContext()->CSSetUnorderedAccessViews( 0, 1, &m_pUAVNULL, NULL );
        DXUTGetD3D11DeviceContext()->CSSetShaderResources( 0, m_iNumInputViews, m_ppNULLInputViews );
        
        TIMER_End() // Vertical Pass
    }
    else // ( ShaderType == SHADER_TYPE_PIXEL )
    {
        DXUTGetD3D11DeviceContext()->PSSetSamplers( 0, 1, &m_pPointSampler );
        DXUTGetD3D11DeviceContext()->PSSetSamplers( 1, 1, &m_pLinearClampSampler );
        DXUTGetD3D11DeviceContext()->PSSetConstantBuffers( 0, 1, &m_pCommonCB );
        
        // Input layout and VS for rendering screen quads
        UINT Stride = sizeof( ScreenQuadVertex );
        UINT Offset = 0;
        DXUTGetD3D11DeviceContext()->IASetInputLayout( m_pScreenInputLayout );
        DXUTGetD3D11DeviceContext()->IASetVertexBuffers( 0, 1, &m_pScreenQuadVertexBuffer, &Stride, &Offset );
        DXUTGetD3D11DeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        DXUTGetD3D11DeviceContext()->VSSetShader( m_pVSTexturedScreenQuad, NULL, 0 );


        TIMER_Begin( 0, L"Horizontal Pass" )
                
        // PS Horizontal filter pass
        DXUTGetD3D11DeviceContext()->OMSetRenderTargets( 1, &m_pRTOutput[0], NULL );
        DXUTGetD3D11DeviceContext()->PSSetShaderResources( 0, m_iNumInputViews, m_ppHorizInputViews );
        DXUTGetD3D11DeviceContext()->PSSetShader( m_pPixelShaders[0], NULL, 0 );
        DXUTGetD3D11DeviceContext()->Draw( 6, 0 );
        DXUTGetD3D11DeviceContext()->OMSetRenderTargets( 1, &m_pRTNULL, NULL );
        DXUTGetD3D11DeviceContext()->PSSetShaderResources( 0, m_iNumInputViews, m_ppNULLInputViews );
        
        TIMER_End() // Horizontal Pass

        TIMER_Begin( 0, L"Vertical Pass" )
        
        // PS Vertical filter pass 
        DXUTGetD3D11DeviceContext()->OMSetRenderTargets( 1, &m_pRTOutput[1], NULL );
        DXUTGetD3D11DeviceContext()->PSSetShaderResources( 0, m_iNumInputViews, m_ppVertInputViews );
        DXUTGetD3D11DeviceContext()->PSSetShader( m_pPixelShaders[1], NULL, 0 );
        DXUTGetD3D11DeviceContext()->Draw( 6, 0 );
        DXUTGetD3D11DeviceContext()->OMSetRenderTargets( 1, &m_pRTNULL, NULL );
        DXUTGetD3D11DeviceContext()->PSSetShaderResources( 0, m_iNumInputViews, m_ppNULLInputViews );
        
        TIMER_End() // Vertical Pass
    }

    // Set back to the original RT
    DXUTGetD3D11DeviceContext()->OMSetRenderTargets( 1, &pOrigRTV, NULL );
    SAFE_RELEASE( pOrigRTV );
}


//--------------------------------------------------------------------------------------
// EOF
//--------------------------------------------------------------------------------------