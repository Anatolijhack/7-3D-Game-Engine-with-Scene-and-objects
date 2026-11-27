#include "Game3.h"
#include <d3d12.h>
#include "d3dx12.h"
#include <d3dcompiler.h>
#include <wrl.h>
#include "Window.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>

Game3::Game3(UINT width, UINT height, std::wstring name) : IDX(width, height, name), m_frameIndex(0),
m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)), m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
m_rtvDescriptorHeapSize(0)
{
}
static float Rnd(float a, float b)
{
	return a + (b - a) * (rand() / (float)RAND_MAX);
}

// квадрат, всегда повЄрнутый к камере (billboard): углы смещаютс€ вдоль Ђправої и Ђвверхї камеры
void Game3::AddBillboard(std::vector<Vertex>& v, DirectX::XMFLOAT3 c, float size, DirectX::XMFLOAT4 col)
{
	auto P = [&](float sx, float sy) -> DirectX::XMFLOAT3
		{
			return { c.x + (m_camRight.x * sx + m_camUp.x * sy) * size,
					 c.y + (m_camRight.y * sx + m_camUp.y * sy) * size,
					 c.z + (m_camRight.z * sx + m_camUp.z * sy) * size };
		};
	v.push_back({ P(-1,  1), col, {0.0f, 0.0f} });
	v.push_back({ P(1,  1), col, {1.0f, 0.0f} });
	v.push_back({ P(1, -1), col, {1.0f, 1.0f} });
	v.push_back({ P(-1,  1), col, {0.0f, 0.0f} });
	v.push_back({ P(1, -1), col, {1.0f, 1.0f} });
	v.push_back({ P(-1, -1), col, {0.0f, 1.0f} });
}

void Game3::UpdateEffects(float dt)
{
	std::vector<Vertex> fx;

	// ореолы вокруг объектов: центры те же, что в Translation в OnUpdate
	float pul = 1.0f + 0.12f * sinf(m_time * 4.0f);
	AddBillboard(fx, { -1.2f, 0.0f, 0.0f }, 1.0f * pul, { 1.0f, 0.63f, 0.16f, 1.0f });   // оранжевый у куба
	AddBillboard(fx, { 1.2f, 0.0f, 0.0f }, 1.1f * pul, { 0.3f, 0.6f, 1.0f, 1.0f });     // синий у шара

	for (int i = 0; i < m_lightMapped->count; ++i)
	{
		const PointLight& L = m_lightMapped->lights[i];
		AddBillboard(fx, L.pos, 0.15f, { L.color.x, L.color.y, L.color.z, 1.0f });
	}
	// рождение частиц между объектами
	m_spawnAccum += 120.0f * dt;                      // 120 штук в секунду
	while (m_spawnAccum >= 1.0f && m_particles.size() < MaxParticles)
	{
		m_spawnAccum -= 1.0f;
		Particle3D p;
		p.pos = { Rnd(-0.1f, 0.1f), -0.5f, Rnd(-0.1f, 0.1f) };
		p.vel = { Rnd(-0.15f, 0.15f), Rnd(0.4f, 0.9f), Rnd(-0.15f, 0.15f) };
		p.maxLife = p.life = Rnd(0.8f, 1.6f);
		p.size = Rnd(0.04f, 0.09f);
		m_particles.push_back(p);
	}
	if (m_spawnAccum > 1.0f) m_spawnAccum = 0.0f;

	// движение и смерть
	for (size_t i = 0; i < m_particles.size(); )
	{
		Particle3D& p = m_particles[i];
		p.life -= dt;
		if (p.life <= 0.0f)
		{
			p = m_particles.back();
			m_particles.pop_back();
			continue;
		}
		p.pos.x += p.vel.x * dt;
		p.pos.y += p.vel.y * dt;
		p.pos.z += p.vel.z * dt;
		++i;
	}

	// вершины частиц: размер и €ркость падают к концу жизни
	for (const Particle3D& p : m_particles)
	{
		float t = p.life / p.maxLife;
		AddBillboard(fx, p.pos, p.size * (0.4f + 0.6f * t), { 1.0f, 0.2f + 0.6f * t, 0.1f * t, t });
	}

	if (fx.size() > MaxFxQuads * 6) fx.resize(MaxFxQuads * 6);
	memcpy(m_fxMapped, fx.data(), fx.size() * sizeof(Vertex));
	m_fxVertexCount = (UINT)fx.size();
}
void Game3::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

void Game3::StepPhysics(float dt)
{
	const float gravity = -9.8f;
	const float floorY = -1.0f;
	const float meshRadius = 0.6f;

	auto radiusOf = [&](const SceneObject& o) { return meshRadius * o.scale; };
	auto invMassOf = [&](const SceneObject& o)
		{
			if (!o.dynamic) return 0.0f;               // статичный = бесконечна€ масса
			return 1.0f / (o.scale * o.scale * o.scale);
		};

	// ---- 1. интегрирование + пол ----
	for (SceneObject& o : m_objects)
	{
		if (!o.dynamic) continue;

		o.vel.y += gravity * dt;
		o.pos.x += o.vel.x * dt;
		o.pos.y += o.vel.y * dt;
		o.pos.z += o.vel.z * dt;

		const float r = radiusOf(o);
		if (o.pos.y - r < floorY)
		{
			o.pos.y = floorY + r;
			if (o.vel.y < 0.0f) o.vel.y = -o.vel.y * o.restitution;
			if (fabsf(o.vel.y) < 0.5f) o.vel.y = 0.0f;
			o.vel.x *= 0.98f;
			o.vel.z *= 0.98f;
		}
	}

	// ---- 2. шар-шар (несколько итераций стабилизируют контакты) ----
	for (int iter = 0; iter < 4; ++iter)
	{
		for (size_t i = 0; i < m_objects.size(); ++i)
		{
			SceneObject& a = m_objects[i];
			if (a.meshId != 1) continue;

			for (size_t j = i + 1; j < m_objects.size(); ++j)
			{
				SceneObject& b = m_objects[j];
				if (b.meshId != 1) continue;

				const float ia = invMassOf(a), ib = invMassOf(b);
				if (ia + ib == 0.0f) continue;     // оба статичны

				float dx = b.pos.x - a.pos.x;
				float dy = b.pos.y - a.pos.y;
				float dz = b.pos.z - a.pos.z;
				const float rSum = radiusOf(a) + radiusOf(b);
				float dist2 = dx * dx + dy * dy + dz * dz;
				if (dist2 >= rSum * rSum) continue; // не касаютс€

				float dist = sqrtf(dist2);
				float nx, ny, nz;
				if (dist > 1e-5f) { nx = dx / dist; ny = dy / dist; nz = dz / dist; }
				else { nx = 0; ny = 1; nz = 0; dist = 0; }  // центры совпали

				// импульс: только если сближаютс€
				const float vn = (b.vel.x - a.vel.x) * nx
					+ (b.vel.y - a.vel.y) * ny
					+ (b.vel.z - a.vel.z) * nz;
				if (vn < 0.0f)
				{
					const float e = (std::min)(a.restitution, b.restitution);
					const float jImp = -(1.0f + e) * vn / (ia + ib);
					a.vel.x -= jImp * ia * nx;  a.vel.y -= jImp * ia * ny;  a.vel.z -= jImp * ia * nz;
					b.vel.x += jImp * ib * nx;  b.vel.y += jImp * ib * ny;  b.vel.z += jImp * ib * nz;
				}

				// раздвинуть пропорционально обратным массам
				const float slop = 0.01f, percent = 0.8f;
				const float depth = rSum - dist;
				const float corr = (std::max)(depth - slop, 0.0f) / (ia + ib) * percent;
				a.pos.x -= nx * corr * ia;  a.pos.y -= ny * corr * ia;  a.pos.z -= nz * corr * ia;
				b.pos.x += nx * corr * ib;  b.pos.y += ny * corr * ib;  b.pos.z += nz * corr * ib;
			}
		}
	}

	// ---- 3. после раздвижени€ шар мог уйти в пол: подт€нуть обратно ----
	for (SceneObject& o : m_objects)
	{
		if (!o.dynamic) continue;
		const float r = radiusOf(o);
		if (o.pos.y - r < floorY) o.pos.y = floorY + r;
	}
}
void Game3::OnMouseWheel(float delta)
{
	m_radius -= delta * 0.4f;                    // вперЄд приближает, назад отдал€ет
	m_radius = std::clamp(m_radius, 1.5f, 15.0f);
}
void Game3::OnUpdate()
{
	using namespace DirectX;

	m_angle += 0.02f;
	m_time += 0.016f;

	m_lightMapped->ambient = { 0.08f, 0.08f, 0.1f, 1.0f };
	m_lightMapped->count = 3;

	float t = m_time;
	m_lightMapped->lights[0] = { { 2.0f * cosf(t),        0.5f, 2.0f * sinf(t)        }, 5.0f, { 1.0f, 0.1f, 0.1f }, 2.0f }; // красна€
	m_lightMapped->lights[1] = { { 2.0f * cosf(t + 2.09f), 0.5f, 2.0f * sinf(t + 2.09f) }, 5.0f, { 0.1f, 1.0f, 0.1f }, 2.0f }; // зелЄна€
	m_lightMapped->lights[2] = { { 2.0f * cosf(t + 4.19f), 0.5f, 2.0f * sinf(t + 4.19f) }, 5.0f, { 0.2f, 0.4f, 1.0f }, 2.0f }; // син€€
	// --- ввод ---
	bool focused = (GetForegroundWindow() == Window::GetHwnd());
	bool down = focused && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);

	POINT pt;
	GetCursorPos(&pt);

	if (down)
	{
		if (m_dragging)
		{
			m_yaw -= (pt.x - m_lastMouse.x) * 0.005f;
			m_pitch += (pt.y - m_lastMouse.y) * 0.005f;
			m_pitch = std::clamp(m_pitch, -1.5f, 1.5f);
		}
		m_dragging = true;
	}
	else
	{
		m_dragging = false;
	}
	m_lastMouse = pt;

	if (focused)
	{
		if (GetAsyncKeyState(VK_UP) & 0x8000) m_radius -= 0.05f;
		if (GetAsyncKeyState(VK_DOWN) & 0x8000) m_radius += 0.05f;
		m_radius = std::clamp(m_radius, 1.5f, 15.0f);
	}

	// --- камера (один раз, по мыши) ---
	float cp = cosf(m_pitch);
	XMVECTOR eye = XMVectorSet(m_radius * cp * sinf(m_yaw),
		m_radius * sinf(m_pitch),
		-m_radius * cp * cosf(m_yaw),
		1.0f);

	XMMATRIX view = XMMatrixLookAtLH(eye, XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
	XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_weight / (float)m_height, 0.1f, 100.0f);

	m_constants.camPos = { XMVectorGetX(eye), XMVectorGetY(eye), XMVectorGetZ(eye), 1.0f };

	// --- всЄ, что зависит от камеры, считаетс€ уже из нового view ---
	XMStoreFloat4x4(&m_viewProj, XMMatrixTranspose(view * proj));
	XMStoreFloat4x4(&m_invViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, view * proj)));

	XMFLOAT4X4 v;
	XMStoreFloat4x4(&v, view);
	m_camRight = { v._11, v._21, v._31 };
	m_camUp = { v._12, v._22, v._32 };

	XMVECTOR ldir = XMVector3Normalize(XMVectorSet(0.5f, -0.7f, 0.6f, 0.0f));   // то же, что lightDir
	XMVECTOR lpos = XMVectorScale(ldir, -25.0f);
	XMMATRIX lview = XMMatrixLookAtLH(lpos, XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
	XMMATRIX lproj = XMMatrixOrthographicLH(24.0f, 24.0f, 1.0f, 60.0f);          // покрывает пол 16x16
	XMStoreFloat4x4(&m_lightMapped->lightVP, XMMatrixTranspose(lview * lproj));
	// --- объекты ---
	for (SceneObject& o : m_objects)
	{
		XMMATRIX model = XMMatrixScaling(o.scale, o.scale, o.scale)
			* XMMatrixRotationY(m_angle * o.spin)
			* XMMatrixRotationX(m_angle * o.tilt)
			* XMMatrixTranslation(o.pos.x, o.pos.y, o.pos.z);

		XMStoreFloat4x4(&o.mvp, XMMatrixTranspose(model * view * proj));
		XMStoreFloat4x4(&o.world, XMMatrixTranspose(model));
		XMStoreFloat4x4(&o.lightMvp, XMMatrixTranspose(model * lview * lproj));
	}

	XMStoreFloat4x4(&m_identity, XMMatrixIdentity());
	StepPhysics(0.016f);

	m_constants.lightDir = { 0.5f, -0.7f, 0.6f, 0.0f };

	// эффекты после камеры: им нужны m_camRight и m_camUp
	UpdateEffects(0.016f);
}

void Game3::OnRender()
{
	PopulateCommandList();

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	m_swapChain->Present(1, 0);

	WaitforPreviousFrame();
}

void Game3::OnDestroy()
{
	WaitforPreviousFrame();
	CloseHandle(m_fenceEvent);
}



void Game3::LoadPipeline()
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
			my_device->CreateRenderTargetView(m_renderTargets[frame].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorHeapSize);
		}
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
	my_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
}

void Game3::LoadAssets()
{
	{
		// 1. —оздаЄм Root Parameters
		CD3DX12_DESCRIPTOR_RANGE1 range[1];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsConstants(44, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &range[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[2].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
			D3D12_SHADER_VISIBILITY_PIXEL);   // b1


		D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
		samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplers[0].MipLODBias = 0;
		samplers[0].MaxAnisotropy = 1;
		samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		samplers[0].MinLOD = 0.0f;
		samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[0].ShaderRegister = 0;
		samplers[0].RegisterSpace = 0;
		samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


		samplers[1] = samplers[0];
		samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;   // за пределами карты: свет
		samplers[1].ShaderRegister = 1;
		// 2. —оздаЄм описание Root Signature
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescriptor;
		rootSignatureDescriptor.Init_1_1(_countof(rootParameters), rootParameters, 2, samplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		// 3. —ериализуем Root Signature
		Microsoft::WRL::ComPtr<ID3DBlob> signature;
		Microsoft::WRL::ComPtr<ID3DBlob> error;

		HRESULT hr = D3D12SerializeVersionedRootSignature(
			&rootSignatureDescriptor,
			&signature,
			&error
		);

		if (FAILED(hr))
		{
			if (error)
				OutputDebugStringA(
					(char*)error->GetBufferPointer()
				);

			throw std::runtime_error("Root Signature serialization failed");
		}

		// 4. —оздаЄм насто€щий Root Signature
		hr = my_device->CreateRootSignature(
			0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(&m_rootSignature)
		);

		if (FAILED(hr))
		{
			throw std::runtime_error("Root Signature creation failed");
		}
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

		HRESULT hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders3d.hlsl").c_str(),
			nullptr, nullptr, "VSMain", "vs_5_0", compilerFlags, 0, &vertexShader, &errors);
		if (FAILED(hr))
		{
			if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());

			std::wstring path = GetAssettsFullPath(L"shaders3d.hlsl");
			wchar_t msg[1024];
			swprintf_s(msg, L"VS compile failed\nHRESULT: 0x%08X\nPath: %s", (unsigned)hr, path.c_str());
			MessageBoxW(nullptr, msg, L"Shader error", MB_OK | MB_ICONERROR);
			throw std::runtime_error("VS compile failed");
		}

		errors.Reset();
		hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders3d.hlsl").c_str(),
			nullptr, nullptr, "PSMain", "ps_5_0", compilerFlags, 0, &pixelShader, &errors);
		if (FAILED(hr))
		{
			if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
			throw std::runtime_error("PS compile failed");
		}

		Microsoft::WRL::ComPtr<ID3DBlob> skyVS, skyPS;
		errors.Reset();
		hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders3d.hlsl").c_str(),
			nullptr, nullptr, "VSSky", "vs_5_0", compilerFlags, 0, &skyVS, &errors);
		if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); throw std::runtime_error("Sky VS compile failed"); }

		errors.Reset();
		hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders3d.hlsl").c_str(),
			nullptr, nullptr, "PSSky", "ps_5_0", compilerFlags, 0, &skyPS, &errors);
		if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); throw std::runtime_error("Sky PS compile failed"); }

		Microsoft::WRL::ComPtr<ID3DBlob> shadowVS;
		errors.Reset();
		hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders3d.hlsl").c_str(),
			nullptr, nullptr, "VSShadow", "vs_5_0", compilerFlags, 0, &shadowVS, &errors);
		if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); throw std::runtime_error("Shadow VS compile failed"); }

		//Vertex Input Layout
		D3D12_INPUT_ELEMENT_DESC inputElementDescription[] =
		{
		  {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,   0, 0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
	      {"COLOR",   0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
	      {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,      0,28,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
	      {"NORMAL",  0,DXGI_FORMAT_R32G32B32_FLOAT,   0,36,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		};

		//PSD
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDescriptor = {};
		psoDescriptor.InputLayout = { inputElementDescription, _countof(inputElementDescription) };
		psoDescriptor.pRootSignature = m_rootSignature.Get();
		psoDescriptor.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDescriptor.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDescriptor.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDescriptor.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDescriptor.DepthStencilState.StencilEnable = FALSE;
		psoDescriptor.SampleMask = UINT_MAX;
		psoDescriptor.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDescriptor.NumRenderTargets = 1;
		psoDescriptor.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
		psoDescriptor.SampleDesc.Count = 1;
		psoDescriptor.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);   // отсечение задних граней
		psoDescriptor.DepthStencilState.DepthEnable = TRUE;
		psoDescriptor.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDescriptor.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDescriptor.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		my_device->CreateGraphicsPipelineState(&psoDescriptor, IID_PPV_ARGS(&m_pipelineState));

		D3D12_GRAPHICS_PIPELINE_STATE_DESC skyDesc = psoDescriptor;
		skyDesc.InputLayout = { nullptr, 0 };
		skyDesc.VS = CD3DX12_SHADER_BYTECODE(skyVS.Get());
		skyDesc.PS = CD3DX12_SHADER_BYTECODE(skyPS.Get());
		skyDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		skyDesc.DepthStencilState.DepthEnable = FALSE;
		skyDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		my_device->CreateGraphicsPipelineState(&skyDesc, IID_PPV_ARGS(&m_pipelineStateSky));

		D3D12_GRAPHICS_PIPELINE_STATE_DESC shDesc = psoDescriptor;
		shDesc.VS = CD3DX12_SHADER_BYTECODE(shadowVS.Get());
		shDesc.PS = { nullptr, 0 };
		shDesc.NumRenderTargets = 0;
		shDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
		shDesc.RasterizerState.SlopeScaledDepthBias = 2.0f;   // защита от Ђр€биї на наклонных гран€х
		my_device->CreateGraphicsPipelineState(&shDesc, IID_PPV_ARGS(&m_pipelineStateShadow));

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
		psoDescriptor.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		my_device->CreateGraphicsPipelineState(&psoDescriptor, IID_PPV_ARGS(&m_pipelineStateGlow));
	}

	//Create Command Last
	my_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList));

	//Vertex Buffer
	{
		std::vector<Vertex> verts;
		std::vector<UINT16> idx;

		// углы грани перечисл€ютс€ ѕќ „ј—ќ¬ќ… при взгл€де на неЄ снаружи
		auto addFace = [&](DirectX::XMFLOAT3 tl, DirectX::XMFLOAT3 tr,
			DirectX::XMFLOAT3 br, DirectX::XMFLOAT3 bl,
			DirectX::XMFLOAT4 c, DirectX::XMFLOAT3 n)
			{
				UINT16 b = (UINT16)verts.size();
				verts.push_back({ tl, c, {0.0f, 0.0f}, n });
				verts.push_back({ tr, c, {1.0f, 0.0f}, n });
				verts.push_back({ br, c, {1.0f, 1.0f}, n });
				verts.push_back({ bl, c, {0.0f, 1.0f}, n });
				UINT16 f[] = { 0, 1, 2,  0, 2, 3 };
				for (UINT16 i : f) idx.push_back(b + i);
			};

		const float h = 0.5f;
		addFace({ -h, h,-h }, { h, h,-h }, { h,-h,-h }, { -h,-h,-h }, { 1.0f, 0.2f, 0.2f, 1.0f }, { 0, 0,-1 });   // -Z
		addFace({ h, h, h }, { -h, h, h }, { -h,-h, h }, { h,-h, h }, { 0.2f, 1.0f, 0.2f, 1.0f }, { 0, 0, 1 });   // +Z
		addFace({ h, h,-h }, { h, h, h }, { h,-h, h }, { h,-h,-h }, { 0.2f, 0.4f, 1.0f, 1.0f }, { 1, 0, 0 });   // +X
		addFace({ -h, h, h }, { -h, h,-h }, { -h,-h,-h }, { -h,-h, h }, { 1.0f, 1.0f, 0.2f, 1.0f }, { -1, 0, 0 });   // -X
		addFace({ -h, h, h }, { h, h, h }, { h, h,-h }, { -h, h,-h }, { 0.2f, 1.0f, 1.0f, 1.0f }, { 0, 1, 0 });   // +Y
		addFace({ h,-h, h }, { -h,-h, h }, { -h,-h,-h }, { h,-h,-h }, { 1.0f, 0.2f, 1.0f, 1.0f }, { 0,-1, 0 });   // -Y

		m_cube = { (UINT)idx.size(), 0, 0 };
		const UINT floorIndexBase = (UINT)idx.size();
		const float fh = 8.0f;     // половина стороны, пол 16 x 16
		const float fy = -1.0f;    // высота пола
		addFace({ -fh, fy, fh }, { fh, fy, fh }, { fh, fy,-fh }, { -fh, fy,-fh },
			{ 0.6f, 0.65f, 0.7f, 1.0f }, { 0, 1, 0 });
		m_floor = { 6, floorIndexBase, 0 };
		// ---- шар ----
		const UINT sphereVertexBase = (UINT)verts.size();
		const UINT sphereIndexBase = (UINT)idx.size();

		const int stacks = 16;
		const int slices = 32;
		const float radius = 0.6f;
		const float pi = 3.14159265f;

		for (int i = 0; i <= stacks; ++i)
		{
			float phi = pi * i / stacks;
			float y = radius * cosf(phi);
			float ring = radius * sinf(phi);

			for (int j = 0; j <= slices; ++j)
			{
				float theta = 2.0f * pi * j / slices;
				float v = (float)i / stacks;
				DirectX::XMFLOAT4 col = { 1.0f - 0.7f * v, 0.5f, 0.3f + 0.7f * v, 1.0f };
				float nx = sinf(phi) * cosf(theta);
				float ny = cosf(phi);
				float nz = sinf(phi) * sinf(theta);

				verts.push_back({ {radius * nx, radius * ny, radius * nz}, col,
								  {(float)j / slices, v}, {nx, ny, nz} });
			}
		}

		for (int i = 0; i < stacks; ++i)
			for (int j = 0; j < slices; ++j)
			{
				UINT16 a = (UINT16)(i * (slices + 1) + j);   // индексы считаютс€ от Ќ”Ћя шара
				UINT16 b = (UINT16)(a + slices + 1);
				UINT16 f[] = { a, (UINT16)(a + 1), (UINT16)(b + 1),
							   a, (UINT16)(b + 1), b };
				for (UINT16 k : f) idx.push_back(k);
			}

		m_sphere = { (UINT)idx.size() - sphereIndexBase, sphereIndexBase, (INT)sphereVertexBase };

		// помощник: создать upload-буфер и залить в него данные
		auto createBuffer = [&](const void* data, UINT size, Microsoft::WRL::ComPtr<ID3D12Resource>& res)
			{
				CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
				CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(size);
				my_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
					D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
				UINT8* p;
				CD3DX12_RANGE rr(0, 0);
				res->Map(0, &rr, reinterpret_cast<void**>(&p));
				memcpy(p, data, size);
				res->Unmap(0, nullptr);
			};

		const UINT vbSize = (UINT)(verts.size() * sizeof(Vertex));
		const UINT ibSize = (UINT)(idx.size() * sizeof(UINT16));
		createBuffer(verts.data(), vbSize, m_vertexBuffer);
		createBuffer(idx.data(), ibSize, m_indexBuffer);

		m_vertexBufferViews.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferViews.StrideInBytes = sizeof(Vertex);
		m_vertexBufferViews.SizeInBytes = vbSize;

		m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
		m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
		m_indexBufferView.SizeInBytes = ibSize;
	}
	{
		const UINT size = MaxFxQuads * 6 * sizeof(Vertex);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(size);
		my_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_fxBuffer));

		CD3DX12_RANGE rr(0, 0);
		m_fxBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_fxMapped));   // держим замапленным

		m_fxView.BufferLocation = m_fxBuffer->GetGPUVirtualAddress();
		m_fxView.StrideInBytes = sizeof(Vertex);
		m_fxView.SizeInBytes = size;
	}

	Microsoft::WRL::ComPtr<ID3D12Resource> textureUploadHeap;
	{
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 2;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		my_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap));

		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.Width = TextureWidth;
		textureDesc.Height = TextureHeight;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
		my_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_texture));

		const UINT64 uploadSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);
		CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
		my_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureUploadHeap));

		std::vector<UINT8> texels = GetTextureData();

		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = texels.data();
		textureData.RowPitch = TextureWidth * TexturePixelSize;
		textureData.SlicePitch = textureData.RowPitch * TextureHeight;

		UpdateSubresources(m_commandList.Get(), m_texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);

		CD3DX12_RESOURCE_BARRIER texBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_commandList->ResourceBarrier(1, &texBarrier);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		my_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
	}
	{
		CD3DX12_RESOURCE_DESC sd = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_TYPELESS, ShadowMapSize, ShadowMapSize, 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
		D3D12_CLEAR_VALUE cv = {};
		cv.Format = DXGI_FORMAT_D32_FLOAT;
		cv.DepthStencil.Depth = 1.0f;
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
		my_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &sd,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&m_shadowMap));

		D3D12_DESCRIPTOR_HEAP_DESC dh = {};
		dh.NumDescriptors = 1;
		dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		my_device->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&m_shadowDsvHeap));

		D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
		dsv.Format = DXGI_FORMAT_D32_FLOAT;
		dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		my_device->CreateDepthStencilView(m_shadowMap.Get(), &dsv, m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart());

		D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sv.Format = DXGI_FORMAT_R32_FLOAT;
		sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = 1;
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvSlot1(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), 1,
			my_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
		my_device->CreateShaderResourceView(m_shadowMap.Get(), &sv, srvSlot1);

		m_shadowViewport = { 0.0f, 0.0f, (float)ShadowMapSize, (float)ShadowMapSize, 0.0f, 1.0f };
		m_shadowScissor = { 0, 0, (LONG)ShadowMapSize, (LONG)ShadowMapSize };
	}
	{
       const UINT size = (sizeof(LightData) + 255) & ~255u;
	   CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
	   CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(size);
	   my_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
	   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_lightBuffer));
	   CD3DX12_RANGE rr(0, 0);
	   m_lightBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_lightMapped));
    }


	m_objects = {
		//   mesh  pos                    scale spin  tilt  pulse dynamic rest   vel
			{ 2, { 0.0f,  0.0f,  0.0f }, 1.0f,  0.0f, 0.0f, 0.0f },                      // пол
			{ 0, {-1.2f,  0.0f,  0.0f }, 1.0f,  1.0f, 0.5f, 0.0f },                      // куб (пока статичен)
			{ 1, { 1.2f,  0.0f,  0.0f }, 1.0f, -1.0f, 0.0f, 0.1f },                      // шар с ореолом (статичен)
			{ 0, {-3.0f, -0.7f,  2.0f }, 0.6f,  0.5f, 0.0f, 0.0f },
			{ 0, { 3.5f, -0.5f, -2.0f }, 1.0f, -0.3f, 0.0f, 0.0f },

			{ 1, {-3.0f, -0.4f,  3.0f }, 1.0f, 0.7f, 0.0f, 0.0f, true, 0.7f, { 3.0f, 0.0f, 0.0f } },   // летит вправо
			{ 1, { 3.0f, -0.4f,  3.0f }, 0.6f, 0.7f, 0.0f, 0.0f, true, 0.7f, {-3.0f, 0.0f, 0.0f } },   // летит влево, легче
			{ 1, { 0.0f,  3.0f, -3.0f }, 0.8f, 0.7f, 0.0f, 0.0f, true, 0.5f },                         // падает на следующий
			{ 1, { 0.1f, -0.4f, -3.0f }, 1.0f, 0.7f, 0.0f, 0.0f, true, 0.3f },                         // лежит на полу
	};

	//Synchronization objects,upload to GPU
	{
		my_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
		m_fenceValue = 1;

		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			throw std::runtime_error("CreateEvent failed");
		}
		HRESULT hr = m_commandList->Close();

		if (FAILED(hr))
			throw std::runtime_error("CommandList Close failed");

		// ќтправл€ем команды на GPU
		ID3D12CommandList* ppCommandLists[] =
		{
			m_commandList.Get()
		};

		m_commandQueue->ExecuteCommandLists(
			_countof(ppCommandLists),
			ppCommandLists
		);

		WaitforPreviousFrame();
	}
}

void Game3::PopulateCommandList()
{
	m_constants.time = m_time;
	m_constants.mode = 0.0f;

	m_commandAllocator->Reset();
	m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->SetGraphicsRootConstantBufferView(2, m_lightBuffer->GetGPUVirtualAddress());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	CD3DX12_RESOURCE_BARRIER transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_commandList->ResourceBarrier(1, &transitionBarrier);

	CD3DX12_RESOURCE_BARRIER toDepth = CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_commandList->ResourceBarrier(1, &toDepth);

	D3D12_CPU_DESCRIPTOR_HANDLE shDsv = m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &shDsv);
	m_commandList->ClearDepthStencilView(shDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	m_commandList->RSSetViewports(1, &m_shadowViewport);
	m_commandList->RSSetScissorRects(1, &m_shadowScissor);

	m_commandList->SetPipelineState(m_pipelineStateShadow.Get());
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferViews);
	m_commandList->IASetIndexBuffer(&m_indexBufferView);

	for (const SceneObject& o : m_objects)
	{
		if (o.meshId == 2) continue;                 // пол тени не отбрасывает
		m_constants.mvp = o.lightMvp;
		m_constants.pulse = o.pulse;
		m_commandList->SetGraphicsRoot32BitConstants(0, 44, &m_constants, 0);
		const auto& m = (o.meshId == 0) ? m_cube : m_sphere;
		m_commandList->DrawIndexedInstanced(m.indexCount, 1, m.firstIndex, m.baseVertex, 0);
	}

	CD3DX12_RESOURCE_BARRIER toSrv = CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_commandList->ResourceBarrier(1, &toSrv);

	m_commandList->RSSetViewports(1, &m_viewport);   // вернуть основной viewport
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorHeapSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	const float bgClearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, bgClearColor, 0, nullptr);
	m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// небо: рисуем первым, без глубины
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->SetPipelineState(m_pipelineStateSky.Get());
	m_constants.mvp = m_invViewProj;
	m_constants.pulse = 0.0f;
	m_constants.mode = 0.0f;
	m_commandList->SetGraphicsRoot32BitConstants(0, 44, &m_constants, 0);
	m_commandList->DrawInstanced(3, 1, 0, 0);

	m_commandList->SetPipelineState(m_pipelineState.Get());   // вернуть обычный PSO

	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferViews);
	m_commandList->IASetIndexBuffer(&m_indexBufferView);

	ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
	m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	m_commandList->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

	// все объекты сцены: пол, кубы, шары
	for (const SceneObject& o : m_objects)
	{
		m_constants.mvp = o.mvp;
		m_constants.world = o.world;
		m_constants.pulse = o.pulse;
		m_commandList->SetGraphicsRoot32BitConstants(0, 44, &m_constants, 0);

		const auto& m = (o.meshId == 0) ? m_cube : (o.meshId == 1) ? m_sphere : m_floor;
		m_commandList->DrawIndexedInstanced(m.indexCount, 1, m.firstIndex, m.baseVertex, 0);
	}

	// ореолы и частицы
	if (m_fxVertexCount > 0)
	{
		m_commandList->SetPipelineState(m_pipelineStateGlow.Get());

		m_constants.mvp = m_viewProj;
		m_constants.pulse = 0.0f;
		m_constants.mode = 1.0f;
		m_commandList->SetGraphicsRoot32BitConstants(0, 44, &m_constants, 0);

		m_fxView.SizeInBytes = m_fxVertexCount * sizeof(Vertex);
		m_commandList->IASetVertexBuffers(0, 1, &m_fxView);
		m_commandList->DrawInstanced(m_fxVertexCount, 1, 0, 0);
	}

	transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	m_commandList->ResourceBarrier(1, &transitionBarrier);
	m_commandList->Close();
}

void Game3::WaitforPreviousFrame()
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

std::vector<UINT8> Game3::GetTextureData()
{
	const UINT rowPitch = TextureWidth * TexturePixelSize;
	std::vector<UINT8> data(rowPitch * TextureHeight);

	const UINT cell = 16;   // размер клетки в пиксел€х, 256 / 16 = 16 клеток

	for (UINT y = 0; y < TextureHeight; ++y)
		for (UINT x = 0; x < TextureWidth; ++x)
		{
			UINT i = y * rowPitch + x * TexturePixelSize;
			bool light = ((x / cell) + (y / cell)) % 2 == 0;
			UINT8 c = light ? 0xff : 0x50;
			data[i] = c; data[i + 1] = c; data[i + 2] = c; data[i + 3] = 0xff;
		}
	return data;
}

