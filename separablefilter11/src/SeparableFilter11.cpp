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
// File: SeparableFilter11.cpp
//
// Direct3D 11 implementing a highly configurable separable filter with DirectCompute 5.0
//--------------------------------------------------------------------------------------

// DXUT now sits one directory up
#include "..\\..\\DXUT\\Core\\DXUT.h"
#include "..\\..\\DXUT\\Core\\DXUTmisc.h"
#include "..\\..\\DXUT\\Optional\\DXUTgui.h"
#include "..\\..\\DXUT\\Optional\\DXUTCamera.h"
#include "..\\..\\DXUT\\Optional\\DXUTSettingsDlg.h"
#include "..\\..\\DXUT\\Optional\\SDKmisc.h"
#include "..\\..\\DXUT\\Optional\\SDKmesh.h"

// AMD SDK also sits one directory up
#include "..\\..\\AMD_SDK\\inc\\AMD_SDK.h"
#include "..\\..\\AMD_SDK\\inc\\ShaderCacheSampleHelper.h"

// Project includes
#include "resource.h"
#include "SeparableFilter.h"

#pragma warning( disable : 4100 ) // disable unreference formal parameter warnings for /W4 builds

//--------------------------------------------------------------------------------------
// Defines
//--------------------------------------------------------------------------------------

typedef enum _SURFACE_FORMAT_TYPE
{
    SURFACE_FORMAT_TYPE_UNORM_8,
    SURFACE_FORMAT_TYPE_FLOAT_16,
    SURFACE_FORMAT_TYPE_FLOAT_32,
    SURFACE_FORMAT_TYPE_MAX
}SURFACE_FORMAT_TYPE;

static char* g_pszSurfaceFormat[] = 
{
    "DXGI_FORMAT_R8G8B8A8_UNORM",
    "DXGI_FORMAT_R16G16B16A16_FLOAT",
    "DXGI_FORMAT_R32G32B32A32_FLOAT"
};

static char* g_pszLDSPrecision[] = 
{
    "8 Bits",
    "16 Bits",
    "32 Bits"
};

typedef enum _FILTER_TYPE
{
    FILTER_TYPE_GAUSSIAN,
    FILTER_TYPE_BILATERAL,
    FILTER_TYPE_MAX
}FILTER_TYPE;

enum
{
    IDC_TOGGLEFULLSCREEN = 1,
    IDC_TOGGLEREF,
    IDC_CHANGEDEVICE,
    IDC_CHECKBOX_COMPUTE_SHADER,
    IDC_STATIC_SURFACE,
    IDC_COMBOBOX_SURFACE,
    IDC_STATIC_LDS,
    IDC_COMBOBOX_LDS,
    IDC_CHECKBOX_APPROXIMATE_FILTER,
    IDC_STATIC_FILTER_RADIUS,
    IDC_SLIDER_FILTER_RADIUS,
    IDC_RADIO_FILTER_NONE,
    IDC_RADIO_FILTER_GAUSSIAN,
    IDC_RADIO_FILTER_BILATERAL,
    IDC_NUM_CONTROL_IDS
};

const int AMD::g_MaxApplicationControlID = IDC_NUM_CONTROL_IDS;

using namespace DirectX;

//--------------------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------------------
CModelViewerCamera          g_Camera;                   // A model viewing camera
CDXUTDialogResourceManager  g_DialogResourceManager;    // manager for shared resources of dialogs
CD3DSettingsDlg             g_SettingsDlg;              // Device settings dialog
CDXUTTextHelper*            g_pTxtHelper = NULL;

// Scene meshes, and input layout
CDXUTSDKMesh				g_SceneMesh;
static ID3D11InputLayout*   g_pSceneVertexLayout = NULL;
CDXUTSDKMesh			    g_SkyMesh;

// Blend states
ID3D11BlendState*           g_pAlphaState = NULL;
ID3D11BlendState*           g_pOpaqueState = NULL;

// Samplers
ID3D11SamplerState*         g_pLinearSampler = NULL;

// depth buffer data
ID3D11Texture2D*            g_pDepthStencilTexture = NULL;
ID3D11DepthStencilView*     g_pDepthStencilView = NULL;
ID3D11ShaderResourceView*	g_pDepthStencilSRV = NULL;

// The off screen buffers, with various views
ID3D11Texture2D*            g_pSceneTexture[2][SURFACE_FORMAT_TYPE_MAX] = { NULL, NULL, NULL, NULL, NULL, NULL };
ID3D11ShaderResourceView*	g_pSceneTextureSRV[2][SURFACE_FORMAT_TYPE_MAX] = { NULL, NULL, NULL, NULL, NULL, NULL };
ID3D11RenderTargetView*		g_pSceneTextureRTV[2][SURFACE_FORMAT_TYPE_MAX] = { NULL, NULL, NULL, NULL, NULL, NULL };
ID3D11UnorderedAccessView*	g_pSceneTextureUAV[2][SURFACE_FORMAT_TYPE_MAX] = { NULL, NULL, NULL, NULL, NULL, NULL };

// Shaders used for the screen quad render
ID3D11VertexShader*         g_pVSTexturedScreenQuad = NULL;
ID3D11PixelShader*          g_pPSTexturedScreenQuad = NULL;

// Shaders used for the main scene render
ID3D11VertexShader*			g_pSceneVS = NULL;
ID3D11PixelShader*			g_pScenePS = NULL;
ID3D11PixelShader*			g_pSkyPS = NULL;

// Arrays of the various shaders used for filtering
// PS
ID3D11PixelShader*          g_pPSHorizontalFilter[FILTER_TYPE_MAX][SeparableFilter::FILTER_PRECISION_TYPE_MAX][SeparableFilter::KERNEL_RADIUS_TYPE_MAX];
ID3D11PixelShader*          g_pPSVerticalFilter[FILTER_TYPE_MAX][SeparableFilter::FILTER_PRECISION_TYPE_MAX][SeparableFilter::KERNEL_RADIUS_TYPE_MAX];
// CS
ID3D11ComputeShader*        g_pCSHorizontalFilter[FILTER_TYPE_MAX][SeparableFilter::FILTER_PRECISION_TYPE_MAX][SeparableFilter::KERNEL_RADIUS_TYPE_MAX][SeparableFilter::LDS_PRECISION_TYPE_MAX];
ID3D11ComputeShader*        g_pCSVerticalFilter[FILTER_TYPE_MAX][SeparableFilter::FILTER_PRECISION_TYPE_MAX][SeparableFilter::KERNEL_RADIUS_TYPE_MAX][SeparableFilter::LDS_PRECISION_TYPE_MAX];

// Vertex structure, buffer and input layout for rendering full screen quads 
struct QuadVertex
{
    XMFLOAT3 v3Pos;
    XMFLOAT2 v2TexCoord;
};
ID3D11Buffer*	            g_pQuadVertexBuffer = NULL;
ID3D11InputLayout*          g_pInputLayout = NULL;

// State of current filter selection
SURFACE_FORMAT_TYPE                     g_eSurfacePrecisionType = SURFACE_FORMAT_TYPE_UNORM_8;        
SeparableFilter::LDS_PRECISION_TYPE     g_eLDSPrecisionType     = SeparableFilter::LDS_PRECISION_TYPE_16_BIT;    
FILTER_TYPE                             g_eFilterType           = FILTER_TYPE_GAUSSIAN;
SeparableFilter::FILTER_PRECISION_TYPE  g_eFilterPrecisionType  = SeparableFilter::FILTER_PRECISION_TYPE_FULL;
SeparableFilter::KERNEL_RADIUS_TYPE     g_eKernelRadius         = SeparableFilter::KERNEL_RADIUS_TYPE_16;

//--------------------------------------------------------------------------------------
// Set up AMD shader cache here
//--------------------------------------------------------------------------------------
AMD::ShaderCache            g_ShaderCache(AMD::ShaderCache::SHADER_AUTO_RECOMPILE_ENABLED, AMD::ShaderCache::ERROR_DISPLAY_ON_SCREEN);

//--------------------------------------------------------------------------------------
// AMD helper classes defined here
//--------------------------------------------------------------------------------------
static AMD::MagnifyTool     g_MagnifyTool;
static AMD::HUD             g_HUD;
static SeparableFilter      g_SeparableFilter;

// Global boolean for HUD rendering
bool                        g_bRenderHUD = true;


//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------

// Constant buffer layout for transfering data to the utility HLSL functions
struct CB_UTILITY
{
	XMMATRIX f4x4World;					// World matrix for object
	XMMATRIX f4x4WorldViewProjection;	// World * View * Projection matrix  
	XMVECTOR fEyePoint;					// Eye	
};
static ID3D11Buffer*    g_pcbUtility = NULL;                 

// Constants specific to bilateral filters
struct CB_BILATERAL_FILTER
{
    float   fProjParams[4]; // ( [0] = fQTimesZNear, [1] = fQ )
};
static ID3D11Buffer*    g_pCBBilateralFilter = NULL;


//--------------------------------------------------------------------------------------
// Forward declarations 
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing,
                          void* pUserContext );
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext );
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext );
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext );
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext );

bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                       DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext );
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                     void* pUserContext );
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                         const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext );
void CALLBACK OnD3D11DestroyDevice( void* pUserContext );
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime,
                                 float fElapsedTime, void* pUserContext );

void InitApp();
void RenderText();

HRESULT AddShadersToCache();

//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) || defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    // DXUT will create and use the best device (either D3D9 or D3D11) 
    // that is available on the system depending on which D3D callbacks are set below

    // Set DXUT callbacks
    DXUTSetCallbackMsgProc( MsgProc );
    DXUTSetCallbackKeyboard( OnKeyboard );
    DXUTSetCallbackFrameMove( OnFrameMove );
    DXUTSetCallbackDeviceChanging( ModifyDeviceSettings );

    DXUTSetCallbackD3D11DeviceAcceptable( IsD3D11DeviceAcceptable );
    DXUTSetCallbackD3D11DeviceCreated( OnD3D11CreateDevice );
    DXUTSetCallbackD3D11SwapChainResized( OnD3D11ResizedSwapChain );
    DXUTSetCallbackD3D11SwapChainReleasing( OnD3D11ReleasingSwapChain );
    DXUTSetCallbackD3D11DeviceDestroyed( OnD3D11DestroyDevice );
    DXUTSetCallbackD3D11FrameRender( OnD3D11FrameRender );

    InitApp();
    DXUTInit( true, true, NULL ); // Parse the command line, show msgboxes on error, no extra command line params
    DXUTSetCursorSettings( true, true );
    DXUTCreateWindow( L"SeparableFilter11 v1.5" );

    // Require D3D_FEATURE_LEVEL_11_0
    DXUTCreateDevice( D3D_FEATURE_LEVEL_11_0, true, 1920, 1080 );

    DXUTMainLoop(); // Enter into the DXUT render loop

	// Ensure the ShaderCache aborts if in a lengthy generation process
	g_ShaderCache.Abort();

    return DXUTGetExitCode();
}


//--------------------------------------------------------------------------------------
// Initialize the app 
//--------------------------------------------------------------------------------------
void InitApp()
{
    D3DCOLOR DlgColor = 0x88888888; // Semi-transparent background for the dialog
    //D3DCOLOR DlgColor = 0x00888888; // Fully-transparent background for the dialog

    g_SettingsDlg.Init( &g_DialogResourceManager );
    g_HUD.m_GUI.Init( &g_DialogResourceManager );
    g_HUD.m_GUI.SetBackgroundColors( DlgColor );
    g_HUD.m_GUI.SetCallback( OnGUIEvent );

    // Don't allow MSAA, since this sample does fullscreen filtering 
    // as a post process, when input buffers are assumed to be non-MSAA
    g_SettingsDlg.GetDialogControl()->GetControl( DXUTSETTINGSDLG_D3D11_MULTISAMPLE_COUNT )->SetEnabled( false );
    g_SettingsDlg.GetDialogControl()->GetControl( DXUTSETTINGSDLG_D3D11_MULTISAMPLE_QUALITY )->SetEnabled( false );

    int iY = AMD::HUD::iElementDelta;

    g_HUD.m_GUI.AddButton( IDC_TOGGLEFULLSCREEN, L"Toggle full screen", AMD::HUD::iElementOffset, iY, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight );
    g_HUD.m_GUI.AddButton( IDC_TOGGLEREF, L"Toggle REF (F3)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, VK_F3 );
    g_HUD.m_GUI.AddButton( IDC_CHANGEDEVICE, L"Change device (F2)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, VK_F2 );

	g_ShaderCache.SetShowShaderISAFlag( false );
	AMD::InitApp( g_ShaderCache, g_HUD, iY );

    iY += AMD::HUD::iGroupDelta;
    
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_COMPUTE_SHADER, L"Use Compute Shader", AMD::HUD::iElementOffset, iY, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, true );
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_APPROXIMATE_FILTER, L"Approximate Filter", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, ( g_eFilterPrecisionType == SeparableFilter::FILTER_PRECISION_TYPE_APPROXIMATE ) );

    iY += AMD::HUD::iGroupDelta;

    g_HUD.m_GUI.AddRadioButton( IDC_RADIO_FILTER_NONE, 1, L"No Filter", AMD::HUD::iElementOffset, iY, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, false, L'1' );
    g_HUD.m_GUI.AddRadioButton( IDC_RADIO_FILTER_GAUSSIAN, 1, L"Gaussian Filter", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, ( g_eFilterType == FILTER_TYPE_GAUSSIAN ), L'2' );
    g_HUD.m_GUI.AddRadioButton( IDC_RADIO_FILTER_BILATERAL, 1, L"Bilateral Filter", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, ( g_eFilterType == FILTER_TYPE_BILATERAL ), L'3' );
    
    iY += AMD::HUD::iGroupDelta;

    g_HUD.m_GUI.AddStatic( IDC_STATIC_FILTER_RADIUS, L"Filter Radius : 16", AMD::HUD::iElementOffset, iY, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight );
    g_HUD.m_GUI.AddSlider( IDC_SLIDER_FILTER_RADIUS, AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, SeparableFilter::KERNEL_RADIUS_TYPE_2, SeparableFilter::KERNEL_RADIUS_TYPE_32, g_eKernelRadius, false );
    
    iY += AMD::HUD::iGroupDelta;
    
    g_HUD.m_GUI.AddStatic( IDC_STATIC_SURFACE, L"Surface Format:", AMD::HUD::iElementOffset, iY, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight );
    CDXUTComboBox* pCombo = NULL;
    g_HUD.m_GUI.AddComboBox( IDC_COMBOBOX_SURFACE, AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, 0, true, &pCombo );
    if( pCombo )
    {
        pCombo->SetDropHeight( AMD::HUD::iElementDropHeight );
               
        pCombo->AddItem( L"R8G8B8A8_U", NULL );
        pCombo->AddItem( L"R16G16B16A16_F", NULL );
        pCombo->AddItem( L"R32G32B32A32_F", NULL );
        
        pCombo->SetSelectedByIndex( g_eSurfacePrecisionType );
    }

    iY += AMD::HUD::iGroupDelta;

    g_HUD.m_GUI.AddStatic( IDC_STATIC_LDS, L"LDS Precision:", AMD::HUD::iElementOffset, iY, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight );
    pCombo = NULL;
    g_HUD.m_GUI.AddComboBox( IDC_COMBOBOX_LDS, AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, 0, true, &pCombo );
    if( pCombo )
    {
        pCombo->SetDropHeight( AMD::HUD::iElementDropHeight );
               
        pCombo->AddItem( L"8 Bits", NULL );
        pCombo->AddItem( L"16 Bits", NULL );
        pCombo->AddItem( L"32 Bits", NULL );
        
        pCombo->SetSelectedByIndex( g_eLDSPrecisionType );
    }

    iY += AMD::HUD::iGroupDelta;

    // Add the magnify tool UI to our HUD
    g_MagnifyTool.InitApp( &g_HUD.m_GUI, iY );
}


//--------------------------------------------------------------------------------------
// Render the help and statistics text. This function uses the ID3DXFont interface for 
// efficient text rendering.
//--------------------------------------------------------------------------------------
void RenderText()
{
    g_pTxtHelper->Begin();
    g_pTxtHelper->SetInsertionPos( 5, 5 );
    g_pTxtHelper->SetForegroundColor( XMVectorSet( 1.0f, 0.0f, 0.0f, 1.0f ) );
    g_pTxtHelper->DrawTextLine( DXUTGetFrameStats( DXUTIsVsyncEnabled() ) );
    g_pTxtHelper->DrawTextLine( DXUTGetDeviceStats() );

    float fFilterTime = (float)TIMER_GetTime( Gpu, L"Filtering" ) * 1000.0f;
    float fHorizontalPassTime = (float)TIMER_GetTime( Gpu, L"Filtering|Horizontal Pass" ) * 1000.0f;
    float fVerticalPassTime = (float)TIMER_GetTime( Gpu, L"Filtering|Vertical Pass" ) * 1000.0f;
    
	WCHAR wcbuf[256];
	swprintf_s( wcbuf, 256, L"Filter cost in milliseconds( Total = %.3f, Horizontal Pass = %.3f, Vertical Pass = %.3f )", fFilterTime, fHorizontalPassTime, fVerticalPassTime );
	g_pTxtHelper->DrawTextLine( wcbuf );

    g_pTxtHelper->SetInsertionPos( 5, DXUTGetDXGIBackBufferSurfaceDesc()->Height - AMD::HUD::iElementDelta );
	g_pTxtHelper->DrawTextLine( L"Toggle GUI    : F1" );

    g_pTxtHelper->End();
}


//--------------------------------------------------------------------------------------
// Reject any D3D11 devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                       DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext )
{
    return true;
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that aren't dependant on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                     void* pUserContext )
{
    HRESULT hr;

    ID3D11DeviceContext* pd3dImmediateContext = DXUTGetD3D11DeviceContext();
    V_RETURN( g_DialogResourceManager.OnD3D11CreateDevice( pd3dDevice, pd3dImmediateContext ) );
    V_RETURN( g_SettingsDlg.OnD3D11CreateDevice( pd3dDevice ) );
    g_pTxtHelper = new CDXUTTextHelper( pd3dDevice, pd3dImmediateContext, &g_DialogResourceManager, 15 );
    
    // Load the scene meshes
	g_SceneMesh.Create( pd3dDevice, L"tank\\tankscene.sdkmesh", false );
    g_SkyMesh.Create( pd3dDevice, L"tank\\desertsky.sdkmesh", false );

	// Linear sampler
    D3D11_SAMPLER_DESC samDesc;
    ZeroMemory( &samDesc, sizeof(samDesc) );
    samDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samDesc.AddressU = samDesc.AddressV = samDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samDesc.MaxAnisotropy = 1;
    samDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samDesc.MaxLOD = D3D11_FLOAT32_MAX;
    V_RETURN( pd3dDevice->CreateSamplerState( &samDesc, &g_pLinearSampler ) );
    DXUT_SetDebugName( g_pLinearSampler, "Linear" );
        
    // Create constant buffers
    D3D11_BUFFER_DESC cbDesc;
    ZeroMemory( &cbDesc, sizeof(cbDesc) );
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof( CB_UTILITY );
    V_RETURN( pd3dDevice->CreateBuffer( &cbDesc, NULL, &g_pcbUtility ) );
    DXUT_SetDebugName( g_pcbUtility, "CB_UTILITY" );
    cbDesc.ByteWidth = sizeof( CB_BILATERAL_FILTER );
    V_RETURN( pd3dDevice->CreateBuffer( &cbDesc, NULL, &g_pCBBilateralFilter ) );
    DXUT_SetDebugName( g_pCBBilateralFilter, "CB_BILATERAL_FILTER" );

    // Fill out a unit quad
    QuadVertex QuadVertices[6];
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
    BD.ByteWidth = sizeof( QuadVertex ) * 6;
    BD.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    BD.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    BD.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    InitData.pSysMem = QuadVertices;
    V_RETURN( pd3dDevice->CreateBuffer( &BD, &InitData, &g_pQuadVertexBuffer ) )
    DXUT_SetDebugName( g_pQuadVertexBuffer, "QuadVB" );
            
    // Setup the camera's view parameters
    g_Camera.SetViewParams( XMVectorSet( 0.0f, 1.80683f, -9.83415f, 1.0f ), XMVectorSet( -1.0f, 1.9f, 0.0f, 1.0f ) );
    
    // Hooks to various AMD helper classes
    g_MagnifyTool.OnCreateDevice( pd3dDevice );
    g_HUD.OnCreateDevice( pd3dDevice );
    g_SeparableFilter.OnCreateDevice( pd3dDevice );

    // Create blend states 
    D3D11_BLEND_DESC BlendStateDesc;
    BlendStateDesc.AlphaToCoverageEnable = FALSE;
    BlendStateDesc.IndependentBlendEnable = FALSE;
    BlendStateDesc.RenderTarget[0].BlendEnable = TRUE;
    BlendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    BlendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; 
    BlendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; 
    BlendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    BlendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA; 
    BlendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; 
    BlendStateDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    V_RETURN( pd3dDevice->CreateBlendState( &BlendStateDesc, &g_pAlphaState ) );
    BlendStateDesc.RenderTarget[0].BlendEnable = FALSE;
    V_RETURN( pd3dDevice->CreateBlendState( &BlendStateDesc, &g_pOpaqueState ) );

    // Generate shaders ( this is an async operation - call AMD::ShaderCache::ShadersReady() to find out if they are complete ) 
    static bool bFirstPass = true;
    if( bFirstPass )
    {
		// Add shaders to the cache
		AddShadersToCache();
		g_ShaderCache.GenerateShaders( AMD::ShaderCache::CREATE_TYPE_COMPILE_CHANGES );       // Only compile shaders that have changed (development mode)
        bFirstPass = false;
    }
    
	// Initialize the timer object
    TIMER_Init( pd3dDevice )

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that depend on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                         const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext )
{
    HRESULT hr;

    V_RETURN( g_DialogResourceManager.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );
    V_RETURN( g_SettingsDlg.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );

    // Setup the camera's projection parameters
    float fAspectRatio = pBackBufferSurfaceDesc->Width / ( FLOAT )pBackBufferSurfaceDesc->Height;
    g_Camera.SetProjParams( XM_PI / 4, fAspectRatio, 0.1f, 125.0f );
    g_Camera.SetWindow( pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height );
    g_Camera.SetButtonMasks( MOUSE_LEFT_BUTTON, MOUSE_WHEEL, MOUSE_MIDDLE_BUTTON );

    // Set the location and size of the AMD standard HUD
    g_HUD.m_GUI.SetLocation( pBackBufferSurfaceDesc->Width - AMD::HUD::iDialogWidth, 0 );
    g_HUD.m_GUI.SetSize( AMD::HUD::iDialogWidth, pBackBufferSurfaceDesc->Height );
    
    // Scene buffer creation 
    for( int iSurface = 0; iSurface < SURFACE_FORMAT_TYPE_MAX; ++iSurface )
    {
        DXGI_FORMAT Format;
        switch( iSurface )
        {
            case SURFACE_FORMAT_TYPE_FLOAT_32:
                Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;

            case SURFACE_FORMAT_TYPE_FLOAT_16:
                Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                break;

            case SURFACE_FORMAT_TYPE_UNORM_8:
                Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                break;

            default:
                Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                break;
        }

        V_RETURN( AMD::CreateSurface( &g_pSceneTexture[0][iSurface], &g_pSceneTextureSRV[0][iSurface], &g_pSceneTextureRTV[0][iSurface], &g_pSceneTextureUAV[0][iSurface],
                            Format, pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height, 1 ) );
        DXUT_SetDebugName( g_pSceneTexture[0][iSurface], "Scene0" );
        DXUT_SetDebugName( g_pSceneTextureSRV[0][iSurface], "Scene0 SRV" );
        DXUT_SetDebugName( g_pSceneTextureRTV[0][iSurface], "Scene0 RTV" );
        DXUT_SetDebugName( g_pSceneTextureUAV[0][iSurface], "Scene0 UAV" );

        V_RETURN( AMD::CreateSurface( &g_pSceneTexture[1][iSurface], &g_pSceneTextureSRV[1][iSurface], &g_pSceneTextureRTV[1][iSurface], &g_pSceneTextureUAV[1][iSurface],
                            Format, pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height, 1 ) );
        DXUT_SetDebugName( g_pSceneTexture[1][iSurface], "Scene1" );
        DXUT_SetDebugName( g_pSceneTextureSRV[1][iSurface], "Scene1 SRV" );
        DXUT_SetDebugName( g_pSceneTextureRTV[1][iSurface], "Scene1 RTV" );
        DXUT_SetDebugName( g_pSceneTextureUAV[1][iSurface], "Scene1 UAV" );
    }
    
    // Create our own depth stencil surface thats bindable as a shader resource
    V_RETURN( AMD::CreateDepthStencilSurface( &g_pDepthStencilTexture, &g_pDepthStencilSRV, &g_pDepthStencilView, 
        DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height, 1 ) );

    // Magnify tool will capture from the color buffer
    g_MagnifyTool.OnResizedSwapChain( pd3dDevice, pSwapChain, pBackBufferSurfaceDesc, pUserContext, 
        pBackBufferSurfaceDesc->Width - AMD::HUD::iDialogWidth, 0 );
    D3D11_RENDER_TARGET_VIEW_DESC RTDesc;
    ID3D11Resource* pTempRTResource;
    DXUTGetD3D11RenderTargetView()->GetResource( &pTempRTResource );
    DXUTGetD3D11RenderTargetView()->GetDesc( &RTDesc );
    g_MagnifyTool.SetSourceResources( pTempRTResource, RTDesc.Format, 
                DXUTGetDXGIBackBufferSurfaceDesc()->Width, DXUTGetDXGIBackBufferSurfaceDesc()->Height,
                DXUTGetDXGIBackBufferSurfaceDesc()->SampleDesc.Count );
    g_MagnifyTool.SetPixelRegion( 128 );
    g_MagnifyTool.SetScale( 5 );
    SAFE_RELEASE( pTempRTResource );
    
    // AMD HUD hook
    g_HUD.OnResizedSwapChain( pBackBufferSurfaceDesc );

    // AMD SeparableFilter hook
    g_SeparableFilter.OnResizedSwapChain( pBackBufferSurfaceDesc );
      
    return S_OK;
}


//--------------------------------------------------------------------------------------
// Render the scene using the D3D11 device
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime,
                                 float fElapsedTime, void* pUserContext )
{
    // Reset the timer at start of frame
    TIMER_Reset()

    // If the settings dialog is being shown, then render it instead of rendering the app's scene
    if( g_SettingsDlg.IsActive() )
    {
        g_SettingsDlg.OnRender( fElapsedTime );
        return;
    }      

    // Store off original render target, this is the back buffer of the swap chain
    ID3D11RenderTargetView* pOrigRTV = NULL;
    pd3dImmediateContext->OMGetRenderTargets( 1, &pOrigRTV, NULL );

    // Clear the render targets
    float ClearColor[4] = { 0.176f, 0.196f, 0.667f, 1.0f };
    pd3dImmediateContext->ClearRenderTargetView( DXUTGetD3D11RenderTargetView(), ClearColor );
    pd3dImmediateContext->ClearDepthStencilView( g_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0, 0 );    
	  
    // Switch off alpha blending
    float BlendFactor[1] = { 0.0f };
    pd3dImmediateContext->OMSetBlendState( g_pOpaqueState, BlendFactor, 0xffffffff );

    // Set the constant buffers
    HRESULT hr;
    D3D11_MAPPED_SUBRESOURCE MappedResource;
    // Bilateral filter cb
    V( pd3dImmediateContext->Map( g_pCBBilateralFilter, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource ) );
    CB_BILATERAL_FILTER* pCBBilateralFilter = ( CB_BILATERAL_FILTER* )MappedResource.pData;
    pCBBilateralFilter->fProjParams[1] = g_Camera.GetFarClip() / ( g_Camera.GetFarClip() - g_Camera.GetNearClip() ); 
    pCBBilateralFilter->fProjParams[0] = pCBBilateralFilter->fProjParams[1] * g_Camera.GetNearClip(); 
    pd3dImmediateContext->Unmap( g_pCBBilateralFilter, 0 );
    pd3dImmediateContext->PSSetConstantBuffers( 2, 1, &g_pCBBilateralFilter );
    pd3dImmediateContext->CSSetConstantBuffers( 2, 1, &g_pCBBilateralFilter );

	XMMATRIX mWorld = g_Camera.GetWorldMatrix();
    XMMATRIX mView = g_Camera.GetViewMatrix();
    XMMATRIX mProj = g_Camera.GetProjMatrix();
    XMMATRIX mWorldViewProjection = mWorld * mView * mProj;

    // Utility cb
    V( pd3dImmediateContext->Map( g_pcbUtility, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource ) );
    CB_UTILITY* pUtility = ( CB_UTILITY* )MappedResource.pData;
    pUtility->f4x4World = XMMatrixTranspose( mWorld );
	pUtility->f4x4WorldViewProjection = XMMatrixTranspose( mWorldViewProjection );;
	pUtility->fEyePoint = g_Camera.GetEyePt();
	pd3dImmediateContext->Unmap( g_pcbUtility, 0 );
    pd3dImmediateContext->VSSetConstantBuffers( 1, 1, &g_pcbUtility );
	pd3dImmediateContext->PSSetConstantBuffers( 1, 1, &g_pcbUtility );

    // NULL RTV and SRV
    ID3D11RenderTargetView* pNULLRTV = NULL;
    ID3D11ShaderResourceView* pNULLSRV = NULL;

    if( g_ShaderCache.ShadersReady() )
    {
        // Render the scene mesh 
        pd3dImmediateContext->PSSetSamplers( 0, 1, &g_pLinearSampler );
        pd3dImmediateContext->OMSetRenderTargets( 1, &g_pSceneTextureRTV[0][g_eSurfacePrecisionType], g_pDepthStencilView );
        pd3dImmediateContext->IASetInputLayout( g_pSceneVertexLayout );
        pd3dImmediateContext->VSSetShader( g_pSceneVS, NULL, 0 );
	    pd3dImmediateContext->PSSetShader( g_pScenePS, NULL, 0 );
	    g_SceneMesh.Render( pd3dImmediateContext, 1, 2 );
        pd3dImmediateContext->PSSetShader( g_pSkyPS, NULL, 0 );
        g_SkyMesh.Render( pd3dImmediateContext, 1, 2 );
        pd3dImmediateContext->OMSetRenderTargets( 1, &pNULLRTV, NULL );
        
        TIMER_Begin( 0, L"Filtering" )
        
        if( !g_HUD.m_GUI.GetRadioButton( IDC_RADIO_FILTER_NONE )->GetChecked() )
        {
            ID3D11ShaderResourceView* pHorizSRVs[2] = { g_pSceneTextureSRV[0][g_eSurfacePrecisionType], g_pDepthStencilSRV };
            ID3D11ShaderResourceView* pVertSRVs[2] = { g_pSceneTextureSRV[1][g_eSurfacePrecisionType], g_pDepthStencilSRV };
            g_SeparableFilter.SetShaderResourceViews( pHorizSRVs, pVertSRVs, 2 );
            
            if( g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_COMPUTE_SHADER )->GetChecked() )
            {
                g_SeparableFilter.SetUnorderedAccessViews( g_pSceneTextureUAV[1][g_eSurfacePrecisionType], g_pSceneTextureUAV[0][g_eSurfacePrecisionType] );
                g_SeparableFilter.SetComputeShaders( g_pCSHorizontalFilter[g_eFilterType][g_eFilterPrecisionType][g_eKernelRadius][g_eLDSPrecisionType], 
                    g_pCSVerticalFilter[g_eFilterType][g_eFilterPrecisionType][g_eKernelRadius][g_eLDSPrecisionType] );
                g_SeparableFilter.OnRender( SeparableFilter::SHADER_TYPE_COMPUTE );
            }
            else
            {
                g_SeparableFilter.SetRenderTargetViews( g_pSceneTextureRTV[1][g_eSurfacePrecisionType], g_pSceneTextureRTV[0][g_eSurfacePrecisionType] );
                g_SeparableFilter.SetPixelShaders( g_pPSHorizontalFilter[g_eFilterType][g_eFilterPrecisionType][g_eKernelRadius], 
                    g_pPSVerticalFilter[g_eFilterType][g_eFilterPrecisionType][g_eKernelRadius] );
                g_SeparableFilter.OnRender( SeparableFilter::SHADER_TYPE_PIXEL );
            }
        }

        TIMER_End() // Filtering
                
        // Render the off screen buffer to the screen
        UINT Stride = sizeof( QuadVertex );
        UINT Offset = 0;
        pd3dImmediateContext->PSSetSamplers( 0, 1, &g_pLinearSampler );
        pd3dImmediateContext->IASetInputLayout( g_pInputLayout );
        pd3dImmediateContext->IASetVertexBuffers( 0, 1, &g_pQuadVertexBuffer, &Stride, &Offset );
        pd3dImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        pd3dImmediateContext->VSSetShader( g_pVSTexturedScreenQuad, NULL, 0 );
        pd3dImmediateContext->OMSetRenderTargets( 1, &pOrigRTV, NULL );
        pd3dImmediateContext->PSSetShaderResources( 0, 1, &g_pSceneTextureSRV[0][g_eSurfacePrecisionType] );
        pd3dImmediateContext->PSSetShader( g_pPSTexturedScreenQuad, NULL, 0 );
        pd3dImmediateContext->Draw( 6, 0 );
        pd3dImmediateContext->PSSetShaderResources( 0, 1, &pNULLSRV );
    }
    

    // Decrement the counter on the stored back buffer RTV
    SAFE_RELEASE( pOrigRTV );
    
    DXUT_BeginPerfEvent( DXUT_PERFEVENTCOLOR, L"HUD / Stats" );

	AMD::ProcessUIChanges();

    if( g_ShaderCache.ShadersReady() )
    {
        // Render the HUD
        if( g_bRenderHUD )
        {
            g_MagnifyTool.Render();
            g_HUD.OnRender( fElapsedTime );
        }

        RenderText();

		AMD::RenderHUDUpdates( g_pTxtHelper );
    }
    else
    {
        // Render shader cache progress if still processing
        g_ShaderCache.RenderProgress( g_pTxtHelper, 15, XMVectorSet( 1.0f, 1.0f, 0.0f, 1.0f ) );
    }
    
    DXUT_EndPerfEvent();
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11ResizedSwapChain 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext )
{
    g_DialogResourceManager.OnD3D11ReleasingSwapChain();

    SAFE_RELEASE( g_pDepthStencilTexture );
    SAFE_RELEASE( g_pDepthStencilView );
    SAFE_RELEASE( g_pDepthStencilSRV );

    for( int iSurface = 0; iSurface < SURFACE_FORMAT_TYPE_MAX; ++iSurface )
    {
        SAFE_RELEASE( g_pSceneTexture[0][iSurface] );
        SAFE_RELEASE( g_pSceneTextureSRV[0][iSurface] );
        SAFE_RELEASE( g_pSceneTextureRTV[0][iSurface] );
        SAFE_RELEASE( g_pSceneTextureUAV[0][iSurface] );

        SAFE_RELEASE( g_pSceneTexture[1][iSurface] );
        SAFE_RELEASE( g_pSceneTextureSRV[1][iSurface] );
        SAFE_RELEASE( g_pSceneTextureRTV[1][iSurface] );
        SAFE_RELEASE( g_pSceneTextureUAV[1][iSurface] );
    }
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11CreateDevice 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11DestroyDevice( void* pUserContext )
{
    g_DialogResourceManager.OnD3D11DestroyDevice();
    g_SettingsDlg.OnD3D11DestroyDevice();
    DXUTGetGlobalResourceCache().OnDestroyDevice();
    SAFE_DELETE( g_pTxtHelper );

    g_SceneMesh.Destroy();
    g_SkyMesh.Destroy();
    SAFE_RELEASE( g_pSceneVertexLayout );
    SAFE_RELEASE( g_pSceneVS );
	SAFE_RELEASE( g_pScenePS );
    SAFE_RELEASE( g_pSkyPS );

    SAFE_RELEASE( g_pInputLayout );
    SAFE_RELEASE( g_pLinearSampler );

    SAFE_RELEASE( g_pVSTexturedScreenQuad );
    SAFE_RELEASE( g_pPSTexturedScreenQuad );
    
    for( int iFilter = 0; iFilter < FILTER_TYPE_MAX; ++iFilter )
    {
        for( int iFilterPrecision = 0; iFilterPrecision < SeparableFilter::FILTER_PRECISION_TYPE_MAX; ++iFilterPrecision )
        {
            for( int iRadius = 0; iRadius < SeparableFilter::KERNEL_RADIUS_TYPE_MAX; ++iRadius )
            {
                SAFE_RELEASE( g_pPSHorizontalFilter[iFilter][iFilterPrecision][iRadius] );
                SAFE_RELEASE( g_pPSVerticalFilter[iFilter][iFilterPrecision][iRadius] );
                
                for( int iPrecision = 0; iPrecision < SeparableFilter::LDS_PRECISION_TYPE_MAX; ++iPrecision )
                {
                    SAFE_RELEASE( g_pCSHorizontalFilter[iFilter][iFilterPrecision][iRadius][iPrecision] );
                    SAFE_RELEASE( g_pCSVerticalFilter[iFilter][iFilterPrecision][iRadius][iPrecision] );
                }
            }
        }
    }

    SAFE_RELEASE( g_pQuadVertexBuffer );

    SAFE_RELEASE( g_pDepthStencilTexture );
    SAFE_RELEASE( g_pDepthStencilView );
    SAFE_RELEASE( g_pDepthStencilSRV );


    for( int iSurface = 0; iSurface < SURFACE_FORMAT_TYPE_MAX; ++iSurface )
    {
        SAFE_RELEASE( g_pSceneTexture[0][iSurface] );
        SAFE_RELEASE( g_pSceneTextureSRV[0][iSurface] );
        SAFE_RELEASE( g_pSceneTextureRTV[0][iSurface] );
        SAFE_RELEASE( g_pSceneTextureUAV[0][iSurface] );

        SAFE_RELEASE( g_pSceneTexture[1][iSurface] );
        SAFE_RELEASE( g_pSceneTextureSRV[1][iSurface] );
        SAFE_RELEASE( g_pSceneTextureRTV[1][iSurface] );
        SAFE_RELEASE( g_pSceneTextureUAV[1][iSurface] );
    }

    SAFE_RELEASE( g_pCBBilateralFilter );
    SAFE_RELEASE( g_pcbUtility );

	g_ShaderCache.OnDestroyDevice();
    g_MagnifyTool.OnDestroyDevice();
    g_HUD.OnDestroyDevice();

    g_SeparableFilter.OnDestroyDevice();

    SAFE_RELEASE( g_pAlphaState );
    SAFE_RELEASE( g_pOpaqueState );

    TIMER_Destroy()
}


//--------------------------------------------------------------------------------------
// Called right before creating a D3D9 or D3D11 device, allowing the app to modify the device settings as needed
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext )
{
    // For the first device created if its a REF device, optionally display a warning dialog box
    static bool s_bFirstTime = true;
    if( s_bFirstTime )
    {
        s_bFirstTime = false;
        if( pDeviceSettings->d3d11.DriverType == D3D_DRIVER_TYPE_REFERENCE )
        {
            DXUTDisplaySwitchingToREFWarning();
        }

        // Start with vsync disabled
        pDeviceSettings->d3d11.SyncInterval = 0;
    }

    // Don't auto create a depth buffer, as this sample requires a depth buffer 
    // be created such that it's bindable as a shader resource
    pDeviceSettings->d3d11.AutoCreateDepthStencil = false;

    // Don't allow MSAA, since this sample does fullscreen filtering 
    // as a post process, when input buffers are assumed to be non-MSAA
    pDeviceSettings->d3d11.sd.SampleDesc.Count = 1;

    return true;
}


//--------------------------------------------------------------------------------------
// Handle updates to the scene.  This is called regardless of which D3D API is used
//--------------------------------------------------------------------------------------
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext )
{
    // Update the camera's position based on user input 
    g_Camera.FrameMove( fElapsedTime );
}


//--------------------------------------------------------------------------------------
// Handle messages to the application
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing,
                          void* pUserContext )
{
	// Pass messages to dialog resource manager calls so GUI state is updated correctly
    *pbNoFurtherProcessing = g_DialogResourceManager.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;

    // Pass messages to settings dialog if its active
    if( g_SettingsDlg.IsActive() )
    {
        g_SettingsDlg.MsgProc( hWnd, uMsg, wParam, lParam );
        return 0;
    }

    // Give the dialogs a chance to handle the message first
    *pbNoFurtherProcessing = g_HUD.m_GUI.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;

    // Pass all remaining windows messages to camera so it can respond to user input
    g_Camera.HandleMessages( hWnd, uMsg, wParam, lParam );
	
    return 0;
}


//--------------------------------------------------------------------------------------
// Handle key presses
//--------------------------------------------------------------------------------------
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext )
{
    if( bKeyDown )
    {
        switch( nChar )
        {
			case VK_F1:
				g_bRenderHUD = !g_bRenderHUD;
				break;
		}
    }
}


//--------------------------------------------------------------------------------------
// Handles the GUI events
//--------------------------------------------------------------------------------------
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext )
{
    int nSelectedIndex = 0;
    int nTemp = 0;
    WCHAR szTemp[256];

    switch( nControlID )
    {
        case IDC_TOGGLEFULLSCREEN:
            DXUTToggleFullScreen();
            break;

        case IDC_TOGGLEREF:
            DXUTToggleREF();
            break;

        case IDC_CHANGEDEVICE:
            g_SettingsDlg.SetActive( !g_SettingsDlg.IsActive() );
            break;

        case IDC_COMBOBOX_SURFACE:
            nSelectedIndex = ((CDXUTComboBox*)pControl)->GetSelectedIndex();
            g_eSurfacePrecisionType = (SURFACE_FORMAT_TYPE)nSelectedIndex;
            break;

        case IDC_COMBOBOX_LDS:
            nSelectedIndex = ((CDXUTComboBox*)pControl)->GetSelectedIndex();
            g_eLDSPrecisionType = (SeparableFilter::LDS_PRECISION_TYPE)nSelectedIndex;
            break;

        case IDC_CHECKBOX_APPROXIMATE_FILTER:
            g_eFilterPrecisionType = (SeparableFilter::FILTER_PRECISION_TYPE)((CDXUTCheckBox*)pControl)->GetChecked();
            break;

        case IDC_SLIDER_FILTER_RADIUS:
            nTemp = ((CDXUTSlider*)pControl)->GetValue();
            swprintf_s( szTemp, L"Filter Radius : %d", ( nTemp + 1 ) * 2 );
            g_HUD.m_GUI.GetStatic( IDC_STATIC_FILTER_RADIUS )->SetText( szTemp );
            g_eKernelRadius = (SeparableFilter::KERNEL_RADIUS_TYPE)nTemp;
            break;
        
        case IDC_RADIO_FILTER_GAUSSIAN:
            g_eFilterType = ((CDXUTRadioButton*)pControl)->GetChecked() ? ( FILTER_TYPE_GAUSSIAN ) : ( g_eFilterType );
            break;

        case IDC_RADIO_FILTER_BILATERAL:
            g_eFilterType = ((CDXUTRadioButton*)pControl)->GetChecked() ? ( FILTER_TYPE_BILATERAL ) : ( g_eFilterType );
            break;

		default:
			AMD::OnGUIEvent( nEvent, nControlID, pControl, pUserContext );
			break;

    }

    // Call the MagnifyTool gui event handler
    g_MagnifyTool.OnGUIEvent( nEvent, nControlID, pControl, pUserContext );
}


//--------------------------------------------------------------------------------------
// Adds all shaders to the shader cache
//--------------------------------------------------------------------------------------
HRESULT AddShadersToCache()
{
    HRESULT hr = E_FAIL;
    AMD::ShaderCache::Macro Macros[6];
    
    // Ensure all shaders are released
    SAFE_RELEASE( g_pSceneVertexLayout );
    SAFE_RELEASE( g_pInputLayout );
    SAFE_RELEASE( g_pSceneVS );
	SAFE_RELEASE( g_pScenePS );
    SAFE_RELEASE( g_pSkyPS );
    SAFE_RELEASE( g_pVSTexturedScreenQuad );
    SAFE_RELEASE( g_pPSTexturedScreenQuad );
    
    for( int iFilter = 0; iFilter < FILTER_TYPE_MAX; ++iFilter )
    {
        for( int iFilterPrecision = 0; iFilterPrecision < SeparableFilter::FILTER_PRECISION_TYPE_MAX; ++iFilterPrecision )
        {
            for( int iRadius = 0; iRadius < SeparableFilter::KERNEL_RADIUS_TYPE_MAX; ++iRadius )
            {
                SAFE_RELEASE( g_pPSHorizontalFilter[iFilter][iFilterPrecision][iRadius] );
                SAFE_RELEASE( g_pPSVerticalFilter[iFilter][iFilterPrecision][iRadius] );
                                
                for( int iPrecision = 0; iPrecision < SeparableFilter::LDS_PRECISION_TYPE_MAX; ++iPrecision )
                {
                    SAFE_RELEASE( g_pCSHorizontalFilter[iFilter][iFilterPrecision][iRadius][iPrecision] );
                    SAFE_RELEASE( g_pCSVerticalFilter[iFilter][iFilterPrecision][iRadius][iPrecision] );
                }
            }
        }
    }

    const D3D11_INPUT_ELEMENT_DESC SceneLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXTURE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	    { "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    const D3D11_INPUT_ELEMENT_DESC Layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pSceneVS, AMD::ShaderCache::SHADER_TYPE_VERTEX, L"vs_5_0", L"VS_RenderScene",
        L"SeparableFilter11.hlsl", 0, NULL, &g_pSceneVertexLayout, (D3D11_INPUT_ELEMENT_DESC*)SceneLayout, ARRAYSIZE( SceneLayout ) );

    g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pScenePS, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"PS_RenderScene",
        L"SeparableFilter11.hlsl", 0, NULL, NULL, NULL, 0 );

    g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pSkyPS, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"PS_Sky",
        L"SeparableFilter11.hlsl", 0, NULL, NULL, NULL, 0 );

    g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pVSTexturedScreenQuad, AMD::ShaderCache::SHADER_TYPE_VERTEX, L"vs_5_0", L"VSTexturedScreenQuad",
        L"SeparableFilter11.hlsl", 0, NULL, &g_pInputLayout, (D3D11_INPUT_ELEMENT_DESC*)Layout, ARRAYSIZE( Layout ) );

    g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pPSTexturedScreenQuad, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"PSTexturedScreenQuad",
        L"SeparableFilter11.hlsl", 0, NULL, NULL, NULL, 0 );
    
    for( int iFilter = 0; iFilter < FILTER_TYPE_MAX; ++iFilter )
    //int iFilter = FILTER_TYPE_GAUSSIAN;   // Compile a specific shader
    {
        wchar_t wsSourceFile[AMD::ShaderCache::m_uFILENAME_MAX_LENGTH];
        wchar_t wsFilterType[AMD::ShaderCache::m_uFILENAME_MAX_LENGTH];

        switch( iFilter )
        {
        case FILTER_TYPE_GAUSSIAN:
            wcscpy_s( wsSourceFile, AMD::ShaderCache::m_uFILENAME_MAX_LENGTH, L"GaussianFilter.hlsl" ); 
            wcscpy_s( wsFilterType, AMD::ShaderCache::m_uFILENAME_MAX_LENGTH, L"GAUSSIAN_FILTER" ); 
            break;
        case FILTER_TYPE_BILATERAL:
            wcscpy_s( wsSourceFile, AMD::ShaderCache::m_uFILENAME_MAX_LENGTH, L"BilateralFilter.hlsl" ); 
            wcscpy_s( wsFilterType, AMD::ShaderCache::m_uFILENAME_MAX_LENGTH, L"BILATERAL_FILTER" ); 
            break;
        };
        
        for( int iFilterPrecision = 0; iFilterPrecision < SeparableFilter::FILTER_PRECISION_TYPE_MAX; ++iFilterPrecision )
        //int iFilterPrecision = SeparableFilter::FILTER_PRECISION_TYPE_FULL; // Compile a specific shader
        {
            for( int iRadius = 0; iRadius < SeparableFilter::KERNEL_RADIUS_TYPE_MAX; ++iRadius )
            //int iRadius = SeparableFilter::KERNEL_RADIUS_TYPE_16;   // Compile a specific shader
            {
                int iKernelRadius = ( iRadius + 1 ) * 2;

                wcscpy_s( Macros[0].m_wsName, AMD::ShaderCache::m_uMACRO_MAX_LENGTH, wsFilterType );
                Macros[0].m_iValue = 1;
                wcscpy_s( Macros[1].m_wsName, AMD::ShaderCache::m_uMACRO_MAX_LENGTH, L"KERNEL_RADIUS" );
                Macros[1].m_iValue = iKernelRadius;
                wcscpy_s( Macros[2].m_wsName, AMD::ShaderCache::m_uMACRO_MAX_LENGTH, L"USE_APPROXIMATE_FILTER" );
                Macros[2].m_iValue = iFilterPrecision;

                wcscpy_s( Macros[3].m_wsName, AMD::ShaderCache::m_uMACRO_MAX_LENGTH, L"HORIZ" );
                Macros[3].m_iValue = 1;

                g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pPSHorizontalFilter[iFilter][iFilterPrecision][iRadius], AMD::ShaderCache::SHADER_TYPE_PIXEL,
                    L"ps_5_0", L"PSFilterX", wsSourceFile, 4, Macros, NULL, NULL, 0 );

                wcscpy_s( Macros[3].m_wsName, AMD::ShaderCache::m_uMACRO_MAX_LENGTH, L"VERT" );
                Macros[3].m_iValue = 1;

                g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pPSVerticalFilter[iFilter][iFilterPrecision][iRadius], AMD::ShaderCache::SHADER_TYPE_PIXEL,
                    L"ps_5_0", L"PSFilterY", wsSourceFile, 4, Macros, NULL, NULL, 0 );

                for( int iLDSPrecision = 0; iLDSPrecision < SeparableFilter::LDS_PRECISION_TYPE_MAX; ++iLDSPrecision )
                //int iLDSPrecision = SeparableFilter::LDS_PRECISION_TYPE_16_BIT;    // Compile a specific shader
                {
                    wcscpy_s( Macros[4].m_wsName, AMD::ShaderCache::m_uMACRO_MAX_LENGTH, L"USE_COMPUTE_SHADER" );
                    Macros[4].m_iValue = 1;
                    wcscpy_s( Macros[5].m_wsName, AMD::ShaderCache::m_uMACRO_MAX_LENGTH, L"LDS_PRECISION" );
                    
                    switch( iLDSPrecision )
                    {
                    case SeparableFilter::LDS_PRECISION_TYPE_8_BIT:
                        Macros[5].m_iValue = 8;
                        break;
                    case SeparableFilter::LDS_PRECISION_TYPE_16_BIT:
                        Macros[5].m_iValue = 16;
                        break;
                    case SeparableFilter::LDS_PRECISION_TYPE_32_BIT:
                        Macros[5].m_iValue = 32;
                        break;
                    }

                    wcscpy_s( Macros[3].m_wsName, AMD::ShaderCache::m_uMACRO_MAX_LENGTH, L"HORIZ" );
                    Macros[3].m_iValue = 1;

                    g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pCSHorizontalFilter[iFilter][iFilterPrecision][iRadius][iLDSPrecision], AMD::ShaderCache::SHADER_TYPE_COMPUTE,
                        L"cs_5_0", L"CSFilterX", wsSourceFile, 6, Macros, NULL, NULL, 0 );

                    wcscpy_s( Macros[3].m_wsName, AMD::ShaderCache::m_uMACRO_MAX_LENGTH, L"VERT" );
                    Macros[3].m_iValue = 1;

                    g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pCSVerticalFilter[iFilter][iFilterPrecision][iRadius][iLDSPrecision], AMD::ShaderCache::SHADER_TYPE_COMPUTE,
                        L"cs_5_0", L"CSFilterY", wsSourceFile, 6, Macros, NULL, NULL, 0 );
                }
            }
        }
    }
    
    return hr;
}


//--------------------------------------------------------------------------------------
// EOF
//--------------------------------------------------------------------------------------