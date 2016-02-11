#include "../include/GraphicsAPI.h"
#include "../include/ARiftControl.h"
#include "../include/BitMap.h"
#include "../include/OculusHMD.h"
#include "../include/Texture.h"
#include "../include/IDSuEyeInputHandler.h"
#include <d3dcommon.h>
#include <iostream>
#include <cmath>
#include <vector>
#include <utility>
#include <math.h>

using namespace DirectX;

GraphicsAPI::GraphicsAPI()
{
	ariftcontrol_ = 0;

	hinstance_ = GetModuleHandle(NULL);
	applicationName_ = L"Oculus Rift AR";

	swapchain_ = 0;
	device_ = 0;
	devicecontext_ = 0;
	rendertargetview_ = 0;
	depthstencilbuffer_ = 0;
	depthstencilstate_ = 0;
	depthstencilview_ = 0;
	rasterstate_ = 0;

  headCamera_ = 0;
  camera3D_ = 0;
  camera2D_ = 0;
	bitmap_ = 0;
	shader_ = 0;
	illumination_ = 0;

	depthDisabledStencilState_ = 0;

	renderTextureLeft_ = 0;
	eyeWindowLeft_ = 0;
	renderTextureRight_ = 0;
	eyeWindowRight_ = 0;

  modelsMutex_ = CreateMutex(NULL, FALSE, L"Models Mutex");

	screenDepth_ = 0.0f;
	screenNear_ = 0.0f;
	fieldOfView_ = 0.0f;
	screenAspect_ = 0.0f;
}

GraphicsAPI::~GraphicsAPI()
{
  WaitForSingleObject(modelsMutex_, INFINITE);
  ariftcontrol_->graphicsAPI_ = NULL;
  ReleaseMutex(modelsMutex_);
	shutDownD3D();
}

bool GraphicsAPI::InitD3D(int screenWidth, int screenHeight, bool vsync, HWND hwnd, bool fullscreen,
	                        float screenDepth, float screenNear, ARiftControl* arift_control)
{

	screenDepth_ = screenDepth;
	screenNear_ = screenNear;
	fieldOfView_ = (float)XM_PI / 4.0f;
	screenAspect_ = (float)screenWidth / (float)screenHeight;

	ariftcontrol_ = arift_control;

	screenWidth_ =  screenWidth;
	screenHeight_ = screenHeight;

	HRESULT result;
	IDXGIFactory* factory;
	IDXGIAdapter* adapter;
	IDXGIOutput* adapterOutput;
	unsigned int numModes = 0, i = 0, numerator = 0, denominator = 0, stringLength = 0;
	DXGI_MODE_DESC* displayModeList;
	DXGI_ADAPTER_DESC adapterDesc;
	int error;
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	D3D_FEATURE_LEVEL featureLevel;
	ID3D11Texture2D* backBufferPtr;
	D3D11_TEXTURE2D_DESC depthBufferDesc;
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	D3D11_RASTERIZER_DESC rasterDesc;
	D3D11_VIEWPORT viewport;
	float fieldOfView, screenAspect;

	// used for disabling Depth Buffer regarding 2D (Bitmap) - 3D (Models) Rendering on Screen
	D3D11_DEPTH_STENCIL_DESC depthDisabledStencilDesc;

	// Store the vsync setting.
	vsync_enabled_ = vsync;
	// Create a DirectX graphics interface factory.
	result = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
	if (FAILED(result)) 
    return false;

	// Use the factory to create an adapter for the primary graphics interface (video card).
	result = factory->EnumAdapters(0, &adapter);
  if (FAILED(result)) 
    return false;

	// Enumerate the primary adapter output (monitor).
	result = adapter->EnumOutputs(0, &adapterOutput);
  if (FAILED(result)) 
    return false;

	// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
	result = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, NULL);
  if (FAILED(result)) 
    return false;

	// Create a list to hold all the possible display modes for this monitor/video card combination.
	displayModeList = new DXGI_MODE_DESC[numModes];
  if (!displayModeList) 
    return false;

	// Now fill the display mode list structures.
	result = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, displayModeList);
  if (FAILED(result)) 
    return false;

	// Now go through all the display modes and find the one that matches the screen width and height.
	// When a match is found store the numerator and denominator of the refresh rate for that monitor.
	for (i = 0; i<numModes; i++)
	{
		if (displayModeList[i].Width == (unsigned int)screenWidth &&
		    displayModeList[i].Height == (unsigned int)screenHeight)
      {
        numerator = displayModeList[i].RefreshRate.Numerator;
        denominator = displayModeList[i].RefreshRate.Denominator;
      }
	}

	// We now have the numerator and denominator for the refresh rate.The last thing we will retrieve using the adapter is the name of the video card and the amount of memory on the video card.
	// Get the adapter (video card) description.
	result = adapter->GetDesc(&adapterDesc);
  if (FAILED(result)) 
    return false;

	// Store the dedicated video card memory in megabytes.
	videocardmemory_ = (int)(adapterDesc.DedicatedVideoMemory / 1024 / 1024);

	// Convert the name of the video card to a character array and store it.
	error = wcstombs_s(&stringLength, videocarddescription_, 128, adapterDesc.Description, 128);
  if (error != 0) 
    return false;

	// Release the display mode list.
	delete[] displayModeList; displayModeList = 0;

	// Release the adapter output.
	adapterOutput->Release(); adapterOutput = 0;

	// Release the adapter.
	adapter->Release(); adapter = 0;

	// Release the factory.
	factory->Release(); factory = 0;

	// Initialize the swap chain description.
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
	// Set to a single back buffer.
	swapChainDesc.BufferCount = 1;

	// Set the width and height of the back buffer.
	swapChainDesc.BufferDesc.Width = screenWidth;
	swapChainDesc.BufferDesc.Height = screenHeight;

	// Set regular 32-bit surface for the back buffer.
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	// Set the refresh rate of the back buffer.
	if (vsync_enabled_)
	{
		swapChainDesc.BufferDesc.RefreshRate.Numerator = numerator;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = denominator;
	}
	else
	{
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	}

	// Set the usage of the back buffer.
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	// Set the handle for the window to render to.
	swapChainDesc.OutputWindow = hwnd;
	// Turn multisampling off.
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	// Set to full screen or windowed mode.
  swapChainDesc.Windowed = !fullscreen;

	// Set the scan line ordering and scaling to unspecified.
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	// Discard the back buffer contents after presenting.
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	// Don't set the advanced flags.
	swapChainDesc.Flags = 0;
	
	// Set the feature level to DirectX 11.
	featureLevel = D3D_FEATURE_LEVEL_11_0;

	// Create the swap chain, Direct3D device, and Direct3D device context.
	// D3D11_CREATE_DEVICE_DEBUG
	result = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG, &featureLevel, 1,
		                                     D3D11_SDK_VERSION, &swapChainDesc, &swapchain_, &device_, NULL, &devicecontext_);
	if (FAILED(result)) 
    return false;

	// Get the pointer to the back buffer.
	result = swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBufferPtr);
  if (FAILED(result)) 
    return false;

	// Create the render target view with the back buffer pointer.
	result = device_->CreateRenderTargetView(backBufferPtr, NULL, &rendertargetview_);
  if (FAILED(result)) 
    return false;

	// Release pointer to the back buffer as we no longer need it.
	backBufferPtr->Release();
	backBufferPtr = 0;

	// Initialize the description of the depth buffer.
	ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));

	// Set up the description of the depth buffer.
	depthBufferDesc.Width = screenWidth;
  depthBufferDesc.Height = screenHeight;

	depthBufferDesc.MipLevels = 1;
	depthBufferDesc.ArraySize = 1;
	depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthBufferDesc.SampleDesc.Count = 1;
	depthBufferDesc.SampleDesc.Quality = 0;
	depthBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthBufferDesc.CPUAccessFlags = 0;
	depthBufferDesc.MiscFlags = 0;

	// Create the texture for the depth buffer using the filled out description.
	result = device_->CreateTexture2D(&depthBufferDesc, NULL, &depthstencilbuffer_);
  if (FAILED(result)) 
    return false;

	// Initialize the description of the stencil state.
	ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));

	// Set up the description of the stencil state.
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc =      D3D11_COMPARISON_LESS;

	depthStencilDesc.StencilEnable = true;
	depthStencilDesc.StencilReadMask = 0xFF;
	depthStencilDesc.StencilWriteMask = 0xFF;

	// Stencil operations if pixel is front-facing.
	depthStencilDesc.FrontFace.StencilFailOp =      D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthStencilDesc.FrontFace.StencilPassOp =      D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilFunc =        D3D11_COMPARISON_ALWAYS;

	// Stencil operations if pixel is back-facing.
	depthStencilDesc.BackFace.StencilFailOp =      D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	depthStencilDesc.BackFace.StencilPassOp =      D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilFunc =        D3D11_COMPARISON_ALWAYS;

	// Create the depth stencil state.
	result = device_->CreateDepthStencilState(&depthStencilDesc, &depthstencilstate_);
  if (FAILED(result)) 
    return false;

	// Set the depth stencil state.
	devicecontext_->OMSetDepthStencilState(depthstencilstate_, 1);

	// Initailze the depth stencil view.
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format =        DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// Create the depth stencil view.
	result = device_->CreateDepthStencilView(depthstencilbuffer_, &depthStencilViewDesc, &depthstencilview_);
  if (FAILED(result)) 
    return false;

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	devicecontext_->OMSetRenderTargets(1, &rendertargetview_, depthstencilview_);

	// Setup the raster description which will determine how and what polygons will be drawn.
	rasterDesc.AntialiasedLineEnable = false;
	rasterDesc.CullMode = D3D11_CULL_BACK;
	rasterDesc.DepthBias = 0;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = true;
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.FrontCounterClockwise = false;
	rasterDesc.MultisampleEnable = false;
	rasterDesc.ScissorEnable = false;
	rasterDesc.SlopeScaledDepthBias = 0.0f;

	// Create the rasterizer state from the description we just filled out.
	result = device_->CreateRasterizerState(&rasterDesc, &rasterstate_);
  if (FAILED(result)) 
    return false;

	// Now set the rasterizer state.
	devicecontext_->RSSetState(rasterstate_);

	// Setup the viewport for rendering.
	viewport.Width = (float)screenWidth;
	viewport.Height = (float)screenHeight;

	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;

	// Create the viewport.
	devicecontext_->RSSetViewports(1, &viewport);

	// Setup the projection matrix.
	fieldOfView = (float)XM_PI / 4.0f;
  screenAspect = (float)screenWidth / (float)screenHeight;

	// Create the [Monoscope]projection matrix for 3D rendering.
	XMMATRIX projectionMatrix_XmMat = XMMatrixPerspectiveFovLH(fieldOfView, screenAspect, screenNear, screenDepth);
	XMStoreFloat4x4(&projectionmatrix_, projectionMatrix_XmMat);

	std::cout << projectionmatrix_._11 << " " << projectionmatrix_._21 << " " << projectionmatrix_._31 << " " << projectionmatrix_._41 << std::endl;
	std::cout << projectionmatrix_._12 << " " << projectionmatrix_._22 << " " << projectionmatrix_._32 << " " << projectionmatrix_._42 << std::endl;
	std::cout << projectionmatrix_._13 << " " << projectionmatrix_._23 << " " << projectionmatrix_._33 << " " << projectionmatrix_._43 << std::endl;
	std::cout << projectionmatrix_._14 << " " << projectionmatrix_._24 << " " << projectionmatrix_._34 << " " << projectionmatrix_._44 << std::endl;

	// Initialize the world matrix to the identity matrix.
	XMMATRIX worldMatrix_XmMat = XMMatrixIdentity();
	XMStoreFloat4x4(&worldmatrix_, worldMatrix_XmMat);

	// Create an orthographic projection matrix for 2D rendering.
	XMMATRIX orthoMatrix_XmMat = XMMatrixOrthographicLH((float)screenWidth, (float)screenHeight, screenNear, screenDepth);
	XMStoreFloat4x4(&orthomatrix_, orthoMatrix_XmMat);

	// Clear the second depth stencil state before setting the parameters.
	ZeroMemory(&depthDisabledStencilDesc, sizeof(depthDisabledStencilDesc));

	// used for 2D (Bitmap) - 3D (Model) Depth Rendering on Screen
	// Now create a second depth stencil state which turns off the Z buffer for 2D rendering.  The only difference is 
	// that DepthEnable is set to false, all other parameters are the same as the other depth stencil state.
	depthDisabledStencilDesc.DepthEnable = false;
	depthDisabledStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthDisabledStencilDesc.DepthFunc =      D3D11_COMPARISON_LESS;
	depthDisabledStencilDesc.StencilEnable = true;
	depthDisabledStencilDesc.StencilReadMask = 0xFF;
  depthDisabledStencilDesc.StencilWriteMask = 0xFF;
	depthDisabledStencilDesc.FrontFace.StencilFailOp =      D3D11_STENCIL_OP_KEEP;
	depthDisabledStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthDisabledStencilDesc.FrontFace.StencilPassOp =      D3D11_STENCIL_OP_KEEP;
	depthDisabledStencilDesc.FrontFace.StencilFunc =        D3D11_COMPARISON_ALWAYS;
	depthDisabledStencilDesc.BackFace.StencilFailOp =       D3D11_STENCIL_OP_KEEP;
	depthDisabledStencilDesc.BackFace.StencilDepthFailOp =  D3D11_STENCIL_OP_DECR;
	depthDisabledStencilDesc.BackFace.StencilPassOp =       D3D11_STENCIL_OP_KEEP;
	depthDisabledStencilDesc.BackFace.StencilFunc =         D3D11_COMPARISON_ALWAYS;

	// Create the state using the device.
	result = device_->CreateDepthStencilState(&depthDisabledStencilDesc, &depthDisabledStencilState_);
  if (FAILED(result)) 
    return false;

  // Create the 2D camera object and set initial pose.
  camera2D_ = new Camera(0.0f, 0.0f, -10.0f, 0.0f, 0.0f, 0.0f);
  if (!camera2D_)
    return false;

	// Create the 3D camera object and set initial pose.
  headCamera_ = new HeadCamera(0.0f, 0.0f, -10.0f, 0.0f, 0.0f, 0.0f);
  if (!headCamera_)
    return false;

  // Create the 3D camera object and set initial pose.
  camera3D_ = new Camera(0.0f, 0.0f, -10.0f, 0.0f, 0.0f, 0.0f);
  if (!camera3D_)
    return false;

	// Create the model object.
	Model* model = new Model();
	if (!model)
		return false;

	// Initialize the first. model object.
	// result = model_->Initialize(device_, L"data/texture.dds");
	result = model->Initialize(device_, "data/Cube.txt", L"data/grass.dds", 0.0, -6.0, -15.0);
	if (!result)
	{
		MessageBox(hwnd, L"Could not initialize the model 1. object.", L"Error", MB_OK);
		return false;
	}
	model->Scale(0.7);
	WaitForSingleObject(modelsMutex_, INFINITE);
	models_.push_back(model);
	ReleaseMutex(modelsMutex_);

  // Create the second model object.
  model = NULL;
  model = new Model();
  if (!model)
    return false;

  // Initialize the 2. model object and translate
  result = model->Initialize(device_, "data/Cube.txt", L"data/box_0.dds", 0.0, -5.0, -10.0);
  if (!result)
  {
    MessageBox(hwnd, L"Could not initialize the model 2. object.", L"Error", MB_OK);
    return false;
  }
  Camera::Pose3D keyFrameBox1;
  keyFrameBox1.positionX_ = 0.0f;
  keyFrameBox1.positionY_ = 0.0f;
  keyFrameBox1.positionZ_ = -10.0f;
  keyFrameBox1.rotationX_ = 0.0f;
  keyFrameBox1.rotationY_ = 0.0f;
  keyFrameBox1.rotationZ_ = 0.0f;
  Camera::Pose3D keyFrameBox2 = keyFrameBox1;
  Camera::Pose3D keyFrameBox3 = keyFrameBox1;
  keyFrameBox2.rotationX_ = XM_PI;
  keyFrameBox2.rotationY_ = XM_PI;
  keyFrameBox2.rotationZ_ = XM_PI;
  keyFrameBox3.rotationX_ = XM_2PI;
  keyFrameBox3.rotationY_ = XM_2PI;
  keyFrameBox3.rotationZ_ = XM_2PI;
  model->AddKeyFrame(keyFrameBox1, std::chrono::duration<double>(0));
  model->AddKeyFrame(keyFrameBox2, std::chrono::duration<double>(3));
  model->AddKeyFrame(keyFrameBox3, std::chrono::duration<double>(3));
  model->StartAnimation();
  WaitForSingleObject(modelsMutex_, INFINITE);
  models_.push_back(model);
  ReleaseMutex(modelsMutex_);

  // Create the 4. model object the teapot
  model = NULL;
  model = new Model();
  if (!model)
    return false;

  // Initialize the 4. model object and translate
  result = model->Initialize(device_, "data/wt_teapot.obj", L"data/teapot_texture.dds", 10.0, -10.0, 0.0);
  if (!result)
  {
    MessageBox(hwnd, L"Could not initialize the model 4. object.", L"Error", MB_OK);
    return false;
  }

  model->Scale(10);
  Camera::Pose3D keyFrameTeapot1;
  keyFrameTeapot1.positionX_ =  20.0f;
  keyFrameTeapot1.positionY_ =  -5.0f;
  keyFrameTeapot1.positionZ_ =   5.0f;
  keyFrameTeapot1.rotationX_ =   0.0f;
  keyFrameTeapot1.rotationY_ =  XM_PI*1.2f;
  keyFrameTeapot1.rotationZ_ =   0.0f;
  Camera::Pose3D keyFrameTeapot2 = keyFrameTeapot1;
  Camera::Pose3D keyFrameTeapot3 = keyFrameTeapot1;
  Camera::Pose3D keyFrameTeapot4 = keyFrameTeapot1;
  Camera::Pose3D keyFrameTeapot5 = keyFrameTeapot1;
  
  keyFrameTeapot2.rotationZ_ =  -(XM_PI/2.0f)*0.5;
    
  model->AddKeyFrame(keyFrameTeapot1, std::chrono::duration<double>(0));
  model->AddKeyFrame(keyFrameTeapot1, std::chrono::duration<double>(3));
  model->AddKeyFrame(keyFrameTeapot2, std::chrono::duration<double>(3));
  model->AddKeyFrame(keyFrameTeapot2, std::chrono::duration<double>(3));
  model->AddKeyFrame(keyFrameTeapot1, std::chrono::duration<double>(3));
  model->StartAnimation();

  WaitForSingleObject(modelsMutex_, INFINITE);
  models_.push_back(model);
  ReleaseMutex(modelsMutex_);

  highlight_texture_ = new Texture();
  if (!highlight_texture_)
    return false;
  result = highlight_texture_->Initialize(device_, L"data/lucky.dds");
  if (!result)
  {
    std::cout << "Could not load highlight texture. " << std::endl;
    return false;
  }

	// Create the bitmap object.
	bitmap_ = new BitMap();
  if (!bitmap_) 
    return false;
	
	// Initialize the bitmap object.
	if (AR_HMD_ENABLED)
		//result = bitmap_->InitializeCameras(device_, screenWidth, screenHeight, arift_control, screenWidth,  screenHeight);
    result = bitmap_->InitializeCameras(device_, screenWidth, screenHeight, arift_control);
	else
		result = bitmap_->Initialize(device_, screenWidth, screenHeight, L"data/texture.dds", screenWidth, screenHeight);

	if (!result)
	{
		MessageBox(hwnd, L"Could not initialize the bitmap object.", L"Error", MB_OK);
		return false;
	}

	// ------------------- [ Setup Eye Rendering ] ----------------------------

	// [Left] Create the render to texture object.
	renderTextureLeft_ = new RenderTexture();
	if (!renderTextureLeft_)
		return false;

	// Initialize the render to texture object.
	result = renderTextureLeft_->Initialize(device_, screenWidth, screenHeight);
	if (!result)
		return false;

	// Create the debug window object.
	eyeWindowLeft_ = new EyeWindow();
	if (!eyeWindowLeft_)
		return false;

	// Initialize the debug window object.
	// Here we create and initialize a new debug window object. Notice I have made the window size 100x100. 
	// There will obviously be some distortion since we will be mapping a full screen image down to a 100x100 texture.
	// To fix the aspect ratio (if it is important for your purposes) then just make sure the debug window is sized smaller 
	// but with the same aspect ratio. 
	result = eyeWindowLeft_->Initialize(device_, screenWidth, screenHeight, screenWidth / 2,  screenHeight);
	if (!result)
	{
		MessageBox(hwnd, L"Could not initialize the debug window object.", L"Error", MB_OK);
		return false;
	}
	// [Right]
	renderTextureRight_ = new RenderTexture();
	if (!renderTextureRight_)
		return false;

	// Initialize the render to texture object.
  result = renderTextureRight_->Initialize(device_, screenWidth, screenHeight);
	if (!result)
		return false;
	
	// Create the debug window object.
	eyeWindowRight_ = new EyeWindow();
	if (!eyeWindowRight_)
		return false;

	result = eyeWindowRight_->Initialize(device_, screenWidth, screenHeight, screenWidth / 2, screenHeight);
	if (!result)
	{
		MessageBox(hwnd, L"Could not initialize the debug window object.", L"Error", MB_OK);
		return false;
	}
	// ----------------------------------------------------------------------

	// Create the texture shader object.
	shader_ = new Shader();
	if (!shader_)
	{
		return false;
	}

	// Initialize the shader object.
  std::cout << "Compiling Shaders" << std::endl;
	result = shader_->Initialize(device_, hwnd);
	if (!result)
	{
		MessageBox(hwnd, L"Could not initialize the shader object.", L"Error", MB_OK);
		return false;
	}
  std::cout << "Compiling Shaders done" << std::endl;

	// Create the Lighting object.
	illumination_ = new Lighting();
	if (!illumination_)
	{
		return false;
	}

	// Initialize the Lighting object.
	illumination_->SetAmbientColor(0.30f, 0.30f, 0.30f, 1.0f);
	illumination_->SetDiffuseColor(1.0f, 1.0f, 1.0f, 1.0f);
	illumination_->SetDirection(-1.0f, -1.0f, -1.0f);

	return true;
}


bool GraphicsAPI::Frame()
{
	bool result;

	
  camera3D_->SetPositionZ(-10.0f);

  float oculusMotionX, oculusMotionY, oculusMotionZ;
  OculusHMD::instance()->trackMotion(oculusMotionY, oculusMotionX, oculusMotionZ);
  camera3D_->SetRotation(-oculusMotionX, -oculusMotionY, oculusMotionZ);

  headCamera_->headToEyeOffset_.rotationX_ = -oculusMotionX;
  headCamera_->headToEyeOffset_.rotationY_ = -oculusMotionY;
  headCamera_->headToEyeOffset_.rotationZ_ = oculusMotionZ;
	// Render the graphics scene.
	result = Render();
	if (!result)
	{
		return false;
	}

	return true;
}


bool GraphicsAPI::Render()
{
	bool result;

	if (HMD_DISTORTION && AR_HMD_ENABLED)
		OculusHMD::instance()->StartFrames();

	// [Left Eye] The first pass of our render is to a texture now. 
	result = RenderToTexture(renderTextureLeft_, 1);
	if (!result)
	{
		return false;
	}

	// Clear the buffers to begin the scene.
	BeginScene(0.0f, 0.0f, 0.0f, 1.0f);

	if (!HMD_DISTORTION)
	{
		// for 2D rendering
		TurnZBufferOff();
		// Render The Eye Window orthogonal to the screen
		RenderEyeWindow(eyeWindowLeft_, renderTextureLeft_);
		TurnZBufferOn();
	}

	// [Right Eye]  ------------------------------------
	result = RenderToTexture(renderTextureRight_,2);
	if (!result)
	{
		return false;
	}

	if (!HMD_DISTORTION)
	{
		TurnZBufferOff();
		RenderEyeWindow(eyeWindowRight_, renderTextureRight_);
		TurnZBufferOn();
	}

	// [End] Present the rendered scene to the screen.
	if (HMD_DISTORTION && AR_HMD_ENABLED)
		OculusHMD::instance()->RenderDistortion();
	else
	  EndScene();

	return true;
}


bool GraphicsAPI::RenderToTexture(RenderTexture* renderTexture, int cam_id)
{
	bool result;

	// Set the render target to be the render to texture.
	renderTexture->SetRenderTarget(devicecontext_, GetDepthStencilView());
	// Clear the render to texture.
	renderTexture->ClearRenderTarget(devicecontext_, GetDepthStencilView(), 0.0f, 0.0f, 1.0f, 1.0f);

	// Render the scene now and it will draw to the render to texture instead of the back buffer.

	result = RenderScene(cam_id);
	if (!result)
	{
		return false;
	}

	// Reset the render target back to the original back buffer and not the render to texture anymore.
	SetBackBufferRenderTarget();

	return true;
}


bool GraphicsAPI::RenderScene(int cam_id)
{
	XMFLOAT4X4 worldMatrix, viewMatrix, projectionMatrix, orthoMatrix, stereoProjectionMatrix;
	bool result;

	// Get the world, and projection matrices from the camera and d3d objects.
	GetWorldMatrix(worldMatrix);
	GetProjectionMatrix(projectionMatrix);
	GetOrthoMatrix(orthoMatrix);

	// ******************************** || 2D RENDERING || *********************************

	// for 2D rendering
	TurnZBufferOff();

	// Put the bitmap vertex and index buffers on the graphics pipeline to prepare them for drawing.
	result = bitmap_->Render(devicecontext_, ariftcontrol_, cam_id);
	if (!result)
	{
		return false;
	}
	
  Shader::UndistortionBuffer* undistBuffer = 
    cam_id == 1
    ? &(ariftcontrol_->leftCameraParameters_)
    : &(ariftcontrol_->rightCameraParameters_);

  undistBuffer->width = (float)screenWidth_/2.0f;
  undistBuffer->height = (float)screenHeight_;

  // Render the bitmap with the texture shader.
  // Generate the view matrix based on the camera's position.
  camera2D_->Render();
	XMFLOAT4X4 cameraStreamMatrix;
	camera2D_->GetViewMatrix(cameraStreamMatrix);

	result = shader_->Render(devicecontext_, bitmap_->GetIndexCount(), worldMatrix, cameraStreamMatrix, orthoMatrix,
    bitmap_->GetTexture(), undistBuffer, illumination_->GetDirection(), illumination_->GetDiffuseColor(),
		illumination_->GetAmbientColor());

  if (!result)
	{
		return false;
	}

	TurnZBufferOn();

	//// ******************************** || 3D RENDERING || *********************************

	// Translate 2nd virtual camera with idp 62cm on x-axis.
  Camera::Pose3D oldCameraPose = camera3D_->SavePose();
	// left eye translation (Mono Eye (0,0,0);
  // set head center to eye center offset
  headCamera_->headToEyeOffset_.positionX_ = ariftcontrol_->headToEyeOffsetX_; // left / right
  headCamera_->headToEyeOffset_.positionY_ = ariftcontrol_->headToEyeOffsetY_;
  headCamera_->headToEyeOffset_.positionZ_ = ariftcontrol_->headToEyeOffsetZ_;
  headCamera_->SetPosition(ariftcontrol_->cameraPositionX_, ariftcontrol_->cameraPositionY_, ariftcontrol_->cameraPositionZ_);
  // Generate the view matrix based on the camera's position.
  headCamera_->RenderEye(cam_id == 1);
  headCamera_->GetViewMatrix(viewMatrix);

	StereoProjectionTransformation(cam_id);
	GetStereoProjectionMatrix(stereoProjectionMatrix);
  
  // Render the 3D Models
  XMMATRIX worldTranslationMatrix = XMMatrixTranslation(ariftcontrol_->worldOffsetX_, ariftcontrol_->worldOffsetY_, ariftcontrol_->worldOffsetZ_);
  if (models_.empty())
    std::cout << "GraphicsAPI::RenderScene WARNING no models found. models_ is empty! " << std::endl;
  int i = 0;
  for (std::vector<Model*>::iterator model = models_.begin(); model != models_.end(); model++, i++)
  {
    // Put the model vertex and index buffers on the graphics pipeline to prepare them for drawing.
    (*model)->Render(devicecontext_);
    ID3D11ShaderResourceView* model_tex = NULL;
    // highlight model(s) according to ariftcontrols settings
    if ((ariftcontrol_->inputMode_ == ARiftControl::InputMode::MODEL && i == current_model_idx_) // one model
      || ariftcontrol_->inputMode_ == ARiftControl::InputMode::WORLD) // all models
    {
      model_tex = highlight_texture_->GetTexture();
    }
    else // no highlighting
    {
      model_tex = (*model)->GetTexture();
    }

    // calculate full transformation
    XMMATRIX modelTransform = (*model)->GetModelTransformation();
    modelTransform = XMMatrixMultiply(modelTransform, worldTranslationMatrix);
    XMStoreFloat4x4(&worldMatrix, modelTransform);
    result = shader_->Render(devicecontext_, (*model)->GetIndexCount(), worldMatrix, viewMatrix, stereoProjectionMatrix,
			model_tex, illumination_->GetDirection(), illumination_->GetDiffuseColor(), illumination_->GetAmbientColor());

    if (!result)
    {
      std::cout << "GraphicsAPI::RenderScene ERROR could not render model " << i << std::endl;
      return false;
    }
  }

	if (!result)
	{
		return false;
	}

	return true;
}

bool GraphicsAPI::RenderEyeWindow(EyeWindow* eyeWindow, RenderTexture* renderTexture)
{
	static int eye = 0;

	XMFLOAT4X4 worldMatrix, projectionMatrix, orthoMatrix;
	HRESULT result;

	// Get the world, view, and ortho matrices from the camera and d3d objects.
	GetWorldMatrix(worldMatrix);
	GetOrthoMatrix(orthoMatrix);

	// Put the debug window vertex and index buffers on the graphics pipeline to prepare them for drawing.
	if (eye == 0)
	{
		result = eyeWindow->Render(devicecontext_, 0, 0);
		eye = 1;
	}
	else if (eye == 1)
	{
		result = eyeWindow->Render(devicecontext_, screenWidth_ / 2, 0);
		eye = 0;
	}

	if (!result)
	{
		return false;
	}

	XMFLOAT3 oldRotation = camera3D_->GetRotation();
	camera3D_->SetRotation(0.0f, 0.0f, 0.0f);
	camera3D_->Render();
	XMFLOAT4X4 cameraStreamMatrix;
	camera3D_->GetViewMatrix(cameraStreamMatrix);
	
	// Render the debug window using the texture shader.
	result = shader_->Render(devicecontext_, eyeWindow->GetIndexCount(), worldMatrix, cameraStreamMatrix,
		orthoMatrix, renderTexture->GetShaderResourceView(), illumination_->GetDirection(), 
		illumination_->GetDiffuseColor(), illumination_->GetAmbientColor());

	camera3D_->SetRotation(oldRotation.x, oldRotation.y, oldRotation.z);
	if (!result)
	{
		return false;
	}

	return true;
}


void GraphicsAPI::shutDownD3D()
{
	// Release the Lighting object.
	if (illumination_)
	{
		delete illumination_;
		illumination_ = 0;
	}

	// Release the debug window object.
	if (eyeWindowRight_)
	{
		eyeWindowRight_->Shutdown();
		delete eyeWindowRight_;
		eyeWindowRight_ = 0;
	}

	// Release the render to texture object.
	if (renderTextureRight_)
	{
		renderTextureRight_->Shutdown();
		delete renderTextureRight_;
		renderTextureRight_ = 0;
	}

	// Release the debug window object.
	if (eyeWindowLeft_)
	{
		eyeWindowLeft_->Shutdown();
		delete eyeWindowLeft_;
		eyeWindowLeft_ = 0;
	}

	// Release the render to texture object.
	if (renderTextureLeft_)
	{
		renderTextureLeft_->Shutdown();
		delete renderTextureLeft_;
		renderTextureLeft_ = 0;
	}

	// Release the texture shader object.
	if (shader_)
	{
		shader_->Shutdown();
		delete shader_;
		shader_ = 0;
	}

	// Release the bitmap object.
	if (bitmap_)
	{
		bitmap_->Shutdown();
		delete bitmap_;
		bitmap_ = 0;
	}

	if (depthDisabledStencilState_)
	{
		depthDisabledStencilState_->Release();
		depthDisabledStencilState_ = 0;
	}

	// Release the color shader object.
	if (shader_)
	{
		shader_->Shutdown();
		delete shader_;
		shader_ = 0;
	}

  // Release the model objects.
  WaitForSingleObject(modelsMutex_, INFINITE);
  while (models_.size() > 0)
  {
    Model* current_model = models_.back();
    current_model->Shutdown();
    delete current_model;
    current_model = NULL;
    models_.pop_back();
  }
  ReleaseMutex(modelsMutex_);
  CloseHandle(modelsMutex_);

	// Release the camera objects.
  if (headCamera_)
  {
    delete headCamera_;
    headCamera_ = 0;
  }
	if (camera3D_)
	{
		delete camera3D_;
		camera3D_ = 0;
	}
  if (camera2D_)
  {
    delete camera2D_;
    camera2D_ = 0;
  }
  if (highlight_texture_)
  {
    delete highlight_texture_;
    highlight_texture_ = NULL;
  }

	// Before shutting down set to windowed mode or when you release the swap chain it will throw an exception.
	if (swapchain_)
	{
		swapchain_->SetFullscreenState(false, NULL);
	}

	if (rasterstate_)
	{
		rasterstate_->Release();
		rasterstate_ = 0;
	}

	if (depthstencilview_)
	{
		depthstencilview_->Release();
		depthstencilview_ = 0;
	}

	if (depthstencilstate_)
	{
		depthstencilstate_->Release();
		depthstencilstate_ = 0;
	}

	if (depthstencilbuffer_)
	{
		depthstencilbuffer_->Release();
		depthstencilbuffer_ = 0;
	}

	if (rendertargetview_)
	{
		rendertargetview_->Release();
		rendertargetview_ = 0;
	}

	if (devicecontext_)
	{
		devicecontext_->Release();
		devicecontext_ = 0;
	}

	if (device_)
	{
		device_->Release();
		device_ = 0;
	}

	if (swapchain_)
	{
		swapchain_->Release();
		swapchain_ = 0;
	}

	// Remove the window.
	DestroyWindow(window_);
	window_ = NULL;

	// Remove the application instance.
	UnregisterClass(applicationName_, hinstance_);
	hinstance_ = NULL;

	return;
}

void GraphicsAPI::BeginScene(float red, float green, float blue, float alpha)
{
	float color[4];

	// Setup the color to clear the buffer to.
	color[0] = red;
	color[1] = green;
	color[2] = blue;
	color[3] = alpha;

	// Clear the back buffer.
	devicecontext_->ClearRenderTargetView(rendertargetview_, color);

	// Clear the depth buffer.
	devicecontext_->ClearDepthStencilView(depthstencilview_, D3D11_CLEAR_DEPTH, 1.0f, 0);

	return;
}


void GraphicsAPI::EndScene()
{
	// Present the back buffer to the screen since rendering is complete.
	if (vsync_enabled_)
	{
		// Lock to screen refresh rate.
		swapchain_->Present(1, 0);
	}
	else
	{
		// Present as fast as possible.
		swapchain_->Present(0, 0);
	}

	return;
}


ID3D11Device* GraphicsAPI::GetDevice()
{
	return device_;
}


ID3D11DeviceContext* GraphicsAPI::GetDeviceContext()
{
	return devicecontext_;
}

void GraphicsAPI::GetProjectionMatrix(XMFLOAT4X4& projectionMatrix)
{
	projectionMatrix = projectionmatrix_;
	return;
}

void GraphicsAPI::GetStereoProjectionMatrix(XMFLOAT4X4& stereoProjectionMatrix)
{
	stereoProjectionMatrix = stereoprojectionmatrix_;
	return;
}


void GraphicsAPI::GetWorldMatrix(XMFLOAT4X4& worldMatrix)
{
	worldMatrix = worldmatrix_;
	return;
}


void GraphicsAPI::GetOrthoMatrix(XMFLOAT4X4& orthoMatrix)
{
	orthoMatrix = orthomatrix_;
	return;
}


void GraphicsAPI::GetVideoCardInfo(char* cardName, int& memory)
{
	strcpy_s(cardName, 128, videocarddescription_);
	memory = videocardmemory_;
	return;
}


void GraphicsAPI::TurnZBufferOn()
{
	devicecontext_->OMSetDepthStencilState(depthstencilstate_, 1);
	return;
}


void GraphicsAPI::TurnZBufferOff()
{
	devicecontext_->OMSetDepthStencilState(depthDisabledStencilState_, 1);
	return;
}


ID3D11DepthStencilView* GraphicsAPI::GetDepthStencilView()
{
	return depthstencilview_;
}


void GraphicsAPI::SetBackBufferRenderTarget()
{
	// Bind the render target view and depth stencil buffer to the output render pipeline.
	devicecontext_->OMSetRenderTargets(1, &rendertargetview_, depthstencilview_);

	return;
}

void GraphicsAPI::StereoProjectionTransformation(int camID)
{
	Matrix4f proj = ovrMatrix4f_Projection(OculusHMD::instance()->eyeRenderDesc_[camID-1].Fov, screenNear_, screenDepth_, false);
	
	stereoprojectionmatrix_._11 = proj.M[0][0];
	stereoprojectionmatrix_._21 = proj.M[0][1];
	stereoprojectionmatrix_._31 = proj.M[0][2];
	stereoprojectionmatrix_._41 = proj.M[0][3];

	stereoprojectionmatrix_._12 = proj.M[1][0];
	stereoprojectionmatrix_._22 = proj.M[1][1];
	stereoprojectionmatrix_._32 = proj.M[1][2];
	stereoprojectionmatrix_._42 = proj.M[1][3];

	stereoprojectionmatrix_._13 = proj.M[2][0];
	stereoprojectionmatrix_._23 = proj.M[2][1];
	stereoprojectionmatrix_._33 = proj.M[2][2];
	stereoprojectionmatrix_._43 = proj.M[2][3];	

	stereoprojectionmatrix_._14 = proj.M[3][0];
	stereoprojectionmatrix_._24 = proj.M[3][1];
	stereoprojectionmatrix_._34 = proj.M[3][2];
	stereoprojectionmatrix_._44 = proj.M[3][3];
}

int GraphicsAPI::SetNextModelActive()
{
  std::cout << " SetNextModelActive waiting for mutex " << std::endl;
  int new_current_model_idx = -1;
  WaitForSingleObject(modelsMutex_, INFINITE);
  new_current_model_idx = current_model_idx_ = (current_model_idx_ + 1) % models_.size();
  ReleaseMutex(modelsMutex_);
  std::cout << "SetNextModelActive current model idx " << current_model_idx_ << std::endl;
  return new_current_model_idx;
}

int GraphicsAPI::SetPreviousModelActive()
{
  std::cout << " SetPreviousModelActive waiting for mutex " << std::endl;
  int new_current_model_idx = -1;
  WaitForSingleObject(modelsMutex_, INFINITE);
  new_current_model_idx = current_model_idx_ = (current_model_idx_ - 1) % models_.size();
  ReleaseMutex(modelsMutex_);
  std::cout << "SetPreviousModelActive current model idx " << current_model_idx_ << std::endl;
  return new_current_model_idx;
}

Model::State GraphicsAPI::GetCurrentModelState()
{
  Model::State currentState;
  WaitForSingleObject(modelsMutex_, INFINITE);
  currentState = models_[current_model_idx_]->GetCurrentState();
  ReleaseMutex(modelsMutex_);
  return currentState;
}

void GraphicsAPI::SetCurrentModelState(Model::State newState)
{
  WaitForSingleObject(modelsMutex_, INFINITE);
  models_[current_model_idx_]->SetState(newState);
  ReleaseMutex(modelsMutex_);
}