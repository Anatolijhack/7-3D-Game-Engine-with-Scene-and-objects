#include "Game2.h"
#include <d3d12.h>
#include "d3dx12.h"
#include <d3dcompiler.h>
#include <wrl.h>
#include "Window.h"
#include <stdexcept>

static float Rnd(float a, float b)
{
	return a + (b - a) * (rand() / (float)RAND_MAX);
}
void Game2::UpdateParticles(float dt)
{
	// --- рождение ---
	const float emitX = 0.0f, emitY = -0.5f;   // где источник
	const float rate = 120.0f;                 // частиц в секунду

	m_spawnAccum += rate * dt;
	while (m_spawnAccum >= 1.0f && m_particles.size() < MaxParticles)
	{
		m_spawnAccum -= 1.0f;
		Particle p;
		p.x = emitX + Rnd(-0.05f, 0.05f);
		p.y = emitY;
		p.vx = Rnd(-0.08f, 0.08f);
		p.vy = Rnd(0.25f, 0.55f);              // летят вверх
		p.maxLife = p.life = Rnd(0.8f, 1.6f);
		p.size = Rnd(0.02f, 0.05f);
		m_particles.push_back(p);
	}
	if (m_spawnAccum > 1.0f) m_spawnAccum = 0.0f;   // не копим долг при полном пуле

	// --- движение и смерть ---
	for (size_t i = 0; i < m_particles.size(); )
	{
		Particle& p = m_particles[i];
		p.life -= dt;
		if (p.life <= 0.0f)
		{
			p = m_particles.back();            // быстрое удаление: заменить последней
			m_particles.pop_back();
			continue;
		}
		p.x += p.vx * dt;
		p.y += p.vy * dt;
		++i;
	}

	// --- сборка вершин ---
	Vertex* v = reinterpret_cast<Vertex*>(m_particleMapped);
	m_particleVertexCount = 0;

	for (const Particle& p : m_particles)
	{
		float t = p.life / p.maxLife;          // 1 в начале жизни, 0 в конце
		float s = p.size * (0.4f + 0.6f * t);  // со временем уменьшается
		DirectX::XMFLOAT4 col = { 1.0f, 0.2f + 0.6f * t, 0.1f * t, t };  // жёлтый -> красный, гаснет

		float x0 = p.x - s, x1 = p.x + s;
		float y0 = (p.y - s) * aspecrt_ratio, y1 = (p.y + s) * aspecrt_ratio;

		Vertex q[6] =
		{
			{ {x0, y1, 0.0f}, {0.0f, 0.0f}, col },
			{ {x1, y1, 0.0f}, {1.0f, 0.0f}, col },
			{ {x1, y0, 0.0f}, {1.0f, 1.0f}, col },
			{ {x0, y1, 0.0f}, {0.0f, 0.0f}, col },
			{ {x1, y0, 0.0f}, {1.0f, 1.0f}, col },
			{ {x0, y0, 0.0f}, {0.0f, 1.0f}, col },
		};
		memcpy(v + m_particleVertexCount, q, sizeof(q));
		m_particleVertexCount += 6;
	}
}
Game2::Game2(UINT width, UINT height, std::wstring name) : IDX(width, height, name), m_frameIndex(0),
m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)), m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
m_rtvDescriptorHeapSize(0)
{
}

void Game2::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

void Game2::OnUpdate()
{
	UpdateParticles(0.016f);
	m_time += 0.016f;
	for (auto& s : m_shapes)
		s.angle += s.speed;
}

void Game2::OnRender()
{
	PopulateCommandList();

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	m_swapChain->Present(1, 0);

	WaitforPreviousFrame();
}

void Game2::OnDestroy()
{
	WaitforPreviousFrame();
	CloseHandle(m_fenceEvent);
}


//Simple checkboard texture
std::vector<UINT8> Game2::GetTextureData(UINT type)
{

	const UINT8 colors[TextureCount][2][3] =
	{
		{ {0x00,0x00,0x00}, {0xff,0xff,0xff} },   // текстура 0: чёрно-белая
		{ {0xff,0x00,0x00}, {0x00,0x00,0xff} },   // текстура 1: красно-синяя
		{ {0x00,0xa0,0x00}, {0xff,0xdc,0x00} },   // текстура 2: зелёно-жёлтая
	};

	const UINT rowPich = TextureWidth * TexturePizelSize;
	const UINT cellPitch = (rowPich >> 4) ? (rowPich >> 4) : TexturePizelSize; // в байтах
	const UINT cellHeight = (TextureHeight >> 4) ? (TextureHeight >> 4) : 1;
	const UINT textureSize = rowPich * TextureHeight;

	std::vector<UINT8> data(textureSize);
	UINT8* pData = &data[0];
	if (type == 3)   // мягкое светящееся пятно
	{
		for (UINT y = 0; y < TextureHeight; ++y)
			for (UINT x = 0; x < TextureWidth; ++x)
			{
				UINT index = y * rowPich + x * TexturePizelSize;

				float dx = ((float)x + 0.5f) / TextureWidth * 2.0f - 1.0f;   // -1..1
				float dy = ((float)y + 0.5f) / TextureHeight * 2.0f - 1.0f;
				float d = sqrtf(dx * dx + dy * dy);                           // расстояние до центра

				float b = 1.0f - d;
				if (b < 0.0f) b = 0.0f;
				b = b * b;                                                    // мягче затухание

				pData[index] = 0xff;                // R  тёплый оранжевый
				pData[index + 1] = 0xa0;                // G
				pData[index + 2] = 0x28;                // B
				pData[index + 3] = (UINT8)(255.0f * b); // A: яркость пятна
			}
		return data;
	}
	if (type == 4)   // спрайт-лист 4x4: пульсирующий цветной круг
	{
		const UINT cols = 4, rows = 4;
		const UINT cw = TextureWidth / cols;      // ширина ячейки в пикселях
		const UINT ch = TextureHeight / rows;

		for (UINT y = 0; y < TextureHeight; ++y)
			for (UINT x = 0; x < TextureWidth; ++x)
			{
				UINT index = y * rowPich + x * TexturePizelSize;

				UINT frame = (y / ch) * cols + (x / cw);              // номер кадра, 0..15
				float t = frame / 16.0f * 6.2831853f;                 // фаза 0..2π

				float lx = (float)(x % cw) + 0.5f - cw / 2.0f;        // координата внутри ячейки от центра
				float ly = (float)(y % ch) + 0.5f - ch / 2.0f;
				float d = sqrtf(lx * lx + ly * ly);

				float radius = 12.0f + 16.0f * (0.5f + 0.5f * sinf(t));   // радиус меняется по кадрам
				float a = radius - d + 0.5f;                               // мягкий край в 1 пиксель
				if (a < 0.0f) a = 0.0f;
				if (a > 1.0f) a = 1.0f;

				pData[index] = (UINT8)(255.0f * (0.5f + 0.5f * sinf(t)));
				pData[index + 1] = (UINT8)(255.0f * (0.5f + 0.5f * sinf(t + 2.1f)));
				pData[index + 2] = (UINT8)(255.0f * (0.5f + 0.5f * sinf(t + 4.2f)));
				pData[index + 3] = (UINT8)(255.0f * a);
			}
		return data;
	}

	for (UINT index = 0; index < textureSize; index += TexturePizelSize)
	{
		UINT x = index % rowPich;
		UINT y = index / rowPich;
		UINT j = y / cellHeight;
		UINT i = x / cellPitch;
		const UINT k = (i % 2 == j % 2) ? 0 : 1;
		pData[index] = colors[type][k][0];
		pData[index + 1] = colors[type][k][1];
		pData[index + 2] = colors[type][k][2];
		pData[index + 3] = 0xff;
	}

	return data;
}

void Game2::LoadPipeline()
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

	//DescriptorHeaps
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescriptor = {};
		rtvHeapDescriptor.NumDescriptors = FrameCount;
		rtvHeapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDescriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		my_device->CreateDescriptorHeap(&rtvHeapDescriptor, IID_PPV_ARGS(&m_rtvHeap));

		
		//SRV for the Texture;Shader Resource View
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDescriptor = {};
		srvHeapDescriptor.NumDescriptors = TextureCount;
		srvHeapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDescriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		my_device->CreateDescriptorHeap(&srvHeapDescriptor, IID_PPV_ARGS(&m_srvHeap));

		m_rtvDescriptorHeapSize = my_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_srvDescriptorSize = my_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	}
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		for (UINT frame = 0; frame < FrameCount; frame++)
		{
			m_swapChain->GetBuffer(frame, IID_PPV_ARGS(&m_renderTargets[frame]));
			my_device->CreateRenderTargetView(m_renderTargets[frame].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorHeapSize);
		}
	}
	my_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
}

void Game2::LoadAssets()
{
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		//Highest version support. IF SUCCES, HIGHEST version  returned will not be greater then this
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if (FAILED(my_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		CD3DX12_DESCRIPTOR_RANGE1 range[1];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &range[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstants(12, 0, 0, D3D12_SHADER_VISIBILITY_ALL);

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescriptor;
		rootSignatureDescriptor.Init_1_1(_countof(rootParameters),rootParameters,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Microsoft::WRL::ComPtr<ID3DBlob> signature;
		Microsoft::WRL::ComPtr<ID3DBlob> error;
		D3DX12SerializeVersionedRootSignature(&rootSignatureDescriptor, featureData.HighestVersion, &signature, &error);
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

		HRESULT hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders2.hlsl").c_str(),
			nullptr, nullptr, "VSMain", "vs_5_0", compilerFlags, 0, &vertexShader, &errors);
		if (FAILED(hr))
		{
			if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());

			std::wstring path = GetAssettsFullPath(L"shaders2.hlsl");
			wchar_t msg[1024];
			swprintf_s(msg, L"VS compile failed\nHRESULT: 0x%08X\nPath: %s", (unsigned)hr, path.c_str());
			MessageBoxW(nullptr, msg, L"Shader error", MB_OK | MB_ICONERROR);
			throw std::runtime_error("VS compile failed");
		}

		errors.Reset();
		hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders2.hlsl").c_str(),
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
			{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,      0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
	        {"COLOR",   0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		};

		//PSD
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDescriptor = {};
		psoDescriptor.InputLayout = { inputElementDescription, _countof(inputElementDescription) };
		psoDescriptor.pRootSignature = m_rootSignature.Get();
		psoDescriptor.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDescriptor.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDescriptor.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDescriptor.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDescriptor.DepthStencilState.DepthEnable = TRUE;
		psoDescriptor.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDescriptor.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDescriptor.DepthStencilState.StencilEnable = FALSE;
		psoDescriptor.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDescriptor.SampleMask = UINT_MAX;
		psoDescriptor.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDescriptor.NumRenderTargets = 1;
		psoDescriptor.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
		psoDescriptor.SampleDesc.Count = 1;
		HRESULT hrPso = my_device->CreateGraphicsPipelineState(&psoDescriptor, IID_PPV_ARGS(&m_pipelineState));
		if (FAILED(hrPso)) throw std::runtime_error("PSO creation failed");

		// светящийся PSO: аддитивное смешивание, глубину проверяет, но не пишет
		D3D12_RENDER_TARGET_BLEND_DESC glowBlend = {};
		glowBlend.BlendEnable = TRUE;
		glowBlend.LogicOpEnable = FALSE;
		glowBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		glowBlend.DestBlend = D3D12_BLEND_ONE;
		glowBlend.BlendOp = D3D12_BLEND_OP_ADD;
		glowBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
		glowBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
		glowBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		glowBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
		glowBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		psoDescriptor.BlendState.RenderTarget[0] = glowBlend;
		psoDescriptor.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		HRESULT hrGlow = my_device->CreateGraphicsPipelineState(&psoDescriptor, IID_PPV_ARGS(&m_pipelineStateGlow));
		if (FAILED(hrGlow)) throw std::runtime_error("Glow PSO creation failed");


	}

	//Create Command Last
	my_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList));

	//Vertex Buffer
	{
		std::vector<Vertex> vertices;

		auto addTriangle = [&](float cx, float cy, float size, float speed,UINT type)
			{
				UINT first = (UINT)vertices.size();
				vertices.push_back({ {0.0f,  size * aspecrt_ratio, 0.0f}, {0.5f, 0.0f} });
				vertices.push_back({ { size, -size * aspecrt_ratio, 0.0f}, {1.0f, 1.0f} });
				vertices.push_back({ {-size, -size * aspecrt_ratio, 0.0f}, {0.0f, 1.0f} });
				m_shapes.push_back({ first, (UINT)vertices.size() - first, cx, cy, 0.0f, speed,type });
			};

		auto addRect = [&](float cx, float cy, float w, float h, float speed,UINT type)
			{
				UINT first = (UINT)vertices.size();
				float x0 = -w / 2, x1 = w / 2;
				float y0 = (-h / 2) * aspecrt_ratio, y1 = (h / 2) * aspecrt_ratio;

				vertices.push_back({ {x0, y1, 0.0f}, {0.0f, 0.0f} });
				vertices.push_back({ {x1, y1, 0.0f}, {1.0f, 0.0f} });
				vertices.push_back({ {x1, y0, 0.0f}, {1.0f, 1.0f} });
				vertices.push_back({ {x0, y1, 0.0f}, {0.0f, 0.0f} });
				vertices.push_back({ {x1, y0, 0.0f}, {1.0f, 1.0f} });
				vertices.push_back({ {x0, y0, 0.0f}, {0.0f, 1.0f} });
				m_shapes.push_back({ first, (UINT)vertices.size() - first, cx, cy, 0.0f, speed,type });
			};

		auto addCircle = [&](float cx, float cy, float r, float speed,UINT type)
			{
				UINT first = (UINT)vertices.size();
				const int segments = 32;
				const float pi = 3.14159265f;
				for (int i = 0; i < segments; ++i)
				{
					float a0 = 2.0f * pi * i / segments;
					float a1 = 2.0f * pi * (i + 1) / segments;

					vertices.push_back({ {0.0f, 0.0f, 0.0f}, {0.5f, 0.5f} });
					vertices.push_back({ {r * cosf(a1), r * sinf(a1) * aspecrt_ratio, 0.0f},
										 {0.5f + 0.5f * cosf(a1), 0.5f - 0.5f * sinf(a1)} });
					vertices.push_back({ {r * cosf(a0), r * sinf(a0) * aspecrt_ratio, 0.0f},
										 {0.5f + 0.5f * cosf(a0), 0.5f - 0.5f * sinf(a0)} });
				}
				m_shapes.push_back({ first, (UINT)vertices.size() - first, cx, cy, 0.0f, speed,type });
			};

		auto tint = [&](size_t shapeIndex, float r, float g, float b, float a = 1.0f)
			{
				const Shape& s = m_shapes[shapeIndex];
				for (UINT i = s.firstVertex; i < s.firstVertex + s.vertexCount; ++i)
					vertices[i].color = { r, g, b, a };
			};


		addRect(-0.6f, 0.0f, 0.3f, 0.3f, 0.02f, 0);     // левый квадрат
		addTriangle(0.0f, 0.0f, 0.2f, 0.00f, 1);
		addCircle(-0.5f, 0.05f, 0.15f, -0.05f, 2);
		addRect(0.0f, 0.0f, 0.7f, 0.7f, 0.0f, 0);    // свечение
		m_shapes.back().textureIndex = 3;
		m_shapes.back().additive = true;

		tint(0, 1.0f, 0.5f, 0.5f);       // только ПОСЛЕ всех addXxx
		tint(2, 0.4f, 1.0f, 0.4f);
		tint(3, 1.0f, 0.63f, 0.16f);

		addRect(-0.6f, -0.4f, 0.3f, 0.3f, 0.0f, 4);   // текстура 4 = лист
		m_shapes.back().additive = true;              // прозрачный фон исчезает при аддитивном рисовании
		m_shapes.back().animFrames = 16;
		m_shapes.back().animCols = 4;
		m_shapes.back().animRows = 4;
		m_shapes.back().animFps = 8.0f;

		m_shapes[0].z = 0.2f;   // левый квадрат
		m_shapes[1].z = 0.5f;   // треугольник
		m_shapes[2].z = 0.3f;   // круг
		m_shapes[3].z = 0.0f;   // свечение
		m_shapes[4].z = 0.0f;   // спрайт

		m_vertexCount = (UINT)vertices.size();
		const UINT vertexBufferSize = m_vertexCount * sizeof(Vertex);

		CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC resourceDescriptor = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

		my_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDescriptor,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer));

		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);
		m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
		memcpy(pVertexDataBegin, vertices.data(), vertexBufferSize);
		m_vertexBuffer->Unmap(0, nullptr);

		//Vertex Buffer View
		m_vertexBufferViews.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferViews.StrideInBytes = sizeof(Vertex);
		m_vertexBufferViews.SizeInBytes = vertexBufferSize;
	}

	{
		const UINT size = MaxParticles * 6 * sizeof(Vertex);
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);
		my_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_particleBuffer));

		CD3DX12_RANGE readRange(0, 0);
		m_particleBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_particleMapped));

		m_particleView.BufferLocation = m_particleBuffer->GetGPUVirtualAddress();
		m_particleView.StrideInBytes = sizeof(Vertex);
		m_particleView.SizeInBytes = size;
	}
	//Create the texture
	Microsoft::WRL::ComPtr<ID3D12Resource> textureUploadHeaps[TextureCount];

	for (UINT t = 0; t < TextureCount; ++t)
	{
		D3D12_RESOURCE_DESC textureDescriptor = {};
		textureDescriptor.MipLevels = 1;
		textureDescriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDescriptor.Width = TextureWidth;
		textureDescriptor.Height = TextureHeight;
		textureDescriptor.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDescriptor.DepthOrArraySize = 1;
		textureDescriptor.SampleDesc.Count = 1;
		textureDescriptor.SampleDesc.Quality = 0;
		textureDescriptor.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
		my_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &textureDescriptor,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_textures[t]));

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_textures[t].Get(), 0, 1);
		CD3DX12_RESOURCE_DESC bufferDest = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
		my_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDest,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureUploadHeaps[t]));

		std::vector<UINT8> texture = GetTextureData(t);

		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = texture.data();
		textureData.RowPitch = TextureWidth * TexturePizelSize;
		textureData.SlicePitch = textureData.RowPitch * TextureHeight;

		UpdateSubresources(m_commandList.Get(), m_textures[t].Get(), textureUploadHeaps[t].Get(), 0, 0, 1, &textureData);

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_textures[t].Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_commandList->ResourceBarrier(1, &barrier);

		// свой SRV в своей ячейке кучи
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescriptor = {};
		srvDescriptor.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDescriptor.Format = textureDescriptor.Format;
		srvDescriptor.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDescriptor.Texture2D.MipLevels = 1;

		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), t, m_srvDescriptorSize);
		my_device->CreateShaderResourceView(m_textures[t].Get(), &srvDescriptor, srvHandle);
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		my_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap));

		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = DXGI_FORMAT_D32_FLOAT;
		clearValue.DepthStencil.Depth = 1.0f;

		CD3DX12_HEAP_PROPERTIES depthHeapProps(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_D32_FLOAT, m_weight, m_height, 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
		my_device->CreateCommittedResource(&depthHeapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&m_depthBuffer));

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		my_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc,
			m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	m_commandList->Close();
	ID3D12CommandList* ppCommandList[] = { m_commandList.Get() };;
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandList), ppCommandList);

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

void Game2::PopulateCommandList()
{
	m_commandAllocator->Reset();
	m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
	m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	
	


	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	CD3DX12_RESOURCE_BARRIER transitionBarrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	m_commandList->ResourceBarrier(1, &transitionBarrier1);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorHeapSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	const float bgClearColor[] = { 0.1f,0.1f,0.1f,0.1f }; //R G B A
	m_commandList->ClearRenderTargetView(rtvHandle, bgClearColor, 0, nullptr);
	m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferViews);
	for (const Shape& s : m_shapes)
	{

		CD3DX12_GPU_DESCRIPTOR_HANDLE srv(m_srvHeap->GetGPUDescriptorHandleForHeapStart(),
			s.textureIndex, m_srvDescriptorSize);
		m_commandList->SetPipelineState(s.additive ? m_pipelineStateGlow.Get() : m_pipelineState.Get());
		m_commandList->SetGraphicsRootDescriptorTable(0, srv);
		float frame = 0.0f, cols = 1.0f, rows = 1.0f;
		if (s.animFrames > 0)
		{
			frame = (float)((UINT)(m_time * s.animFps) % s.animFrames);   // текущий кадр
			cols = (float)s.animCols;
			rows = (float)s.animRows;
		}
		float constants[12] =
		{
			s.angle, aspecrt_ratio, s.cx, s.cy,        // 0..3
			m_time,                                     // 4
			0.15f * (s.textureIndex == 3),              // 5: пульсация только у свечения
			0.1f * (s.textureIndex == 2),               // 6: движение текстуры только у текстуры 2
			s.z,                                        // 7: глубина (в шейдере переменная depth)
			frame, cols, rows, 0.0f                     // 8..11
		};
		m_commandList->SetGraphicsRoot32BitConstants(1, 12, constants, 0);
		m_commandList->SetGraphicsRoot32BitConstants(1, 12, constants, 0);
		m_commandList->DrawInstanced(s.vertexCount, 1, s.firstVertex, 0);
	}
	if (m_particleVertexCount > 0)
	{
		m_commandList->SetPipelineState(m_pipelineStateGlow.Get());

		CD3DX12_GPU_DESCRIPTOR_HANDLE srv(m_srvHeap->GetGPUDescriptorHandleForHeapStart(),
			3, m_srvDescriptorSize);                       // текстура свечения
		m_commandList->SetGraphicsRootDescriptorTable(0, srv);

		// без поворота, без сдвига, без пульсации
		float constants[12] = { 0.0f, aspecrt_ratio, 0.0f, 0.0f, m_time, 0.0f, 0.0f, 0.0f,
						0.0f, 1.0f, 1.0f, 0.0f };
		m_commandList->SetGraphicsRoot32BitConstants(1, 12, constants, 0);

		m_particleView.SizeInBytes = m_particleVertexCount * sizeof(Vertex);
		m_commandList->IASetVertexBuffers(0, 1, &m_particleView);
		m_commandList->DrawInstanced(m_particleVertexCount, 1, 0, 0);
	}
	CD3DX12_RESOURCE_BARRIER transitionBarrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	m_commandList->ResourceBarrier(1, &transitionBarrier2);
	m_commandList->Close();

}

void Game2::WaitforPreviousFrame()
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

