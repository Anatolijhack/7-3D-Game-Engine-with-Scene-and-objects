#include "Game.h"
#include <d3d12.h>
#include "d3dx12.h"
#include <d3dcompiler.h>
#include <wrl.h>
#include "Window.h"
#include <stdexcept>

Game::Game(UINT width, UINT height, std::wstring name) : IDX(width,height,name),m_frameIndex(0),
m_viewport(0.0f, 0.0f ,static_cast<float>(width),static_cast<float>(height)), m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
m_rtvDescriptorHeapSize(0)
{
}

void Game::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

void Game::OnUpdate()
{
}

void Game::OnRender()
{
	PopulateCommandList();

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	m_swapChain->Present(1, 0);

	WaitforPreviousFrame();
}

void Game::OnDestroy()
{
	WaitforPreviousFrame();
	CloseHandle(m_fenceEvent);
}



void Game::LoadPipeline()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> debugControler;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugControler))))
		{
			debugControler->EnableDebugLayer();
			dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));

	if (m_useWarpAdapter)
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter;
		factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));
		D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&my_device));
	}
	else
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwarweAdapter;
		GetHardwareAdapter(factory.Get(), &hardwarweAdapter);
		D3D12CreateDevice(hardwarweAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&my_device));
	}
	//Command Queue
	D3D12_COMMAND_QUEUE_DESC queueDescriptor = {};
	queueDescriptor.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDescriptor.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	my_device->CreateCommandQueue(&queueDescriptor, IID_PPV_ARGS(&m_commandQueue));
	
	//Swap Chain
	DXGI_SWAP_CHAIN_DESC1 swapChainDescriptor = {};
	swapChainDescriptor.BufferCount = FrameCount;
	swapChainDescriptor.Width = m_weight;
	swapChainDescriptor.Height = m_height;
	swapChainDescriptor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDescriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDescriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDescriptor.SampleDesc.Count = 1;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
	factory->CreateSwapChainForHwnd(m_commandQueue.Get(), Window::GetHwnd(), &swapChainDescriptor,
		nullptr,
		nullptr,
		&swapChain);

	factory->MakeWindowAssociation(Window::GetHwnd(), DXGI_MWA_NO_ALT_ENTER);
	swapChain.As(&m_swapChain);

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescriptor = {};
		rtvHeapDescriptor.NumDescriptors = FrameCount;
		rtvHeapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDescriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		my_device->CreateDescriptorHeap(&rtvHeapDescriptor, IID_PPV_ARGS(&m_rtvHeap));

		m_rtvDescriptorHeapSize = my_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	}
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		for (UINT frame = 0; frame < FrameCount; frame++)
		{
			m_swapChain->GetBuffer(frame, IID_PPV_ARGS(&m_renderTargets[frame]));
			my_device->CreateRenderTargetView(m_renderTargets[frame].Get(),nullptr,rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorHeapSize);
		}
	}
	my_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
}

void Game::LoadAssets()
{
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDescriptor;
		rootSignatureDescriptor.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Microsoft::WRL::ComPtr<ID3DBlob> signature;
		Microsoft::WRL::ComPtr<ID3DBlob> error;
		D3D12SerializeRootSignature(&rootSignatureDescriptor, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
		my_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));

	}
	//Create the Pipeline state :compiling and loading shaders
	{
		Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
		Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
#if  defined(_DEBUG)
		UINT compilerFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compilerFlags = 0;
#endif
		Microsoft::WRL::ComPtr<ID3DBlob> errors;

		HRESULT hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders.hlsl").c_str(),
			nullptr, nullptr, "VSMain", "vs_5_0", compilerFlags, 0, &vertexShader, &errors);
		if (FAILED(hr))
		{
			if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());

			std::wstring path = GetAssettsFullPath(L"shaders.hlsl");
			wchar_t msg[1024];
			swprintf_s(msg, L"VS compile failed\nHRESULT: 0x%08X\nPath: %s", (unsigned)hr, path.c_str());
			MessageBoxW(nullptr, msg, L"Shader error", MB_OK | MB_ICONERROR);
			throw std::runtime_error("VS compile failed");
		}

		errors.Reset();
		hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders.hlsl").c_str(),
			nullptr, nullptr, "PSMain", "ps_5_0", compilerFlags, 0, &pixelShader, &errors);
		if (FAILED(hr))
		{
			if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
			throw std::runtime_error("PS compile failed");
		}
		//Vertex Input Layout
		D3D12_INPUT_ELEMENT_DESC inputElementDescription[] =
		{
			{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
			{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12}
		};

		//PSD
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDescriptor = {};
		psoDescriptor.InputLayout = { inputElementDescription, _countof(inputElementDescription) };
		psoDescriptor.pRootSignature = m_rootSignature.Get();
		psoDescriptor.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDescriptor.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDescriptor.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDescriptor.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDescriptor.DepthStencilState.DepthEnable = FALSE;
		psoDescriptor.DepthStencilState.StencilEnable = FALSE;
		psoDescriptor.SampleMask = UINT_MAX;
		psoDescriptor.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDescriptor.NumRenderTargets = 1;
		psoDescriptor.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
		psoDescriptor.SampleDesc.Count = 1;
		my_device->CreateGraphicsPipelineState(&psoDescriptor, IID_PPV_ARGS(&m_pipelineState));


	}

	//Create Command Last
	my_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList));
	
	m_commandList->Close();

	//Vertex Buffer
	{
		Vertex triangleVertices[] =
		{
			{{ 0.0f,   0.25f * aspecrt_ratio, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},  // верх, красна€
			{{ 0.25f, -0.25f * aspecrt_ratio, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},  // право-низ, зелЄна€
			{{-0.25f, -0.25f * aspecrt_ratio, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},  // лево-низ, син€€
		};
		const UINT vertexBufferSize = sizeof(triangleVertices);

		CD3DX12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

		CD3DX12_RESOURCE_DESC resourceDescriptor = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

		my_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDescriptor, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer));

		UINT* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);
		m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		m_vertexBuffer->Unmap(0, nullptr);

		//Vertex Buffer View
		m_vertexBufferViews.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferViews.StrideInBytes = sizeof(Vertex);
		m_vertexBufferViews.SizeInBytes = vertexBufferSize;
	}

	//Synchronization objects,upload to GPU
	{
		my_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
		m_fenceValue = 1;

		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			throw std::runtime_error("CreateEvent failed");
		}
		WaitforPreviousFrame();
	}

	

}

void Game::PopulateCommandList()
{
	m_commandAllocator->Reset();
	m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()); 
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	CD3DX12_RESOURCE_BARRIER transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	m_commandList->ResourceBarrier(1, &transitionBarrier);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorHeapSize);
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	const float bgClearColor[] = { 0.1f,0.1f,0.1f,0.1f }; //R G B A
	m_commandList->ClearRenderTargetView(rtvHandle, bgClearColor, 0, nullptr);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferViews);
	m_commandList->DrawInstanced(3, 1, 0, 0);

	transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);

	m_commandList->ResourceBarrier(1, &transitionBarrier);
	m_commandList->Close();

}

void Game::WaitforPreviousFrame()
{
	const UINT64 fence = m_fenceValue;
	m_commandQueue->Signal(m_fence.Get(), fence);
	m_fenceValue++;

	if (m_fence->GetCompletedValue() < fence)
	{
		m_fence->SetEventOnCompletion(fence, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
