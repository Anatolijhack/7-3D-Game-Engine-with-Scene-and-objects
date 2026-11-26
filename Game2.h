#pragma once
#include "IDX.h"
#include <wrl.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <DirectXMath.h>
class Game2 : public IDX
{
public:
	Game2(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();
private:
	static const UINT FrameCount = 2;
	static const UINT TextureWidth = 256;
	static const UINT TextureHeight = 256;
	static const UINT TexturePizelSize = 4;
	static const UINT TextureCount = 5;
	static const UINT MaxParticles = 1000;

	struct Vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT2 uv;
		DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	struct Shape
	{
		UINT firstVertex;
		UINT vertexCount;
		float cx, cy;        // центр фигуры
		float angle;         // текущий угол
		float speed;         // скорость вращения
		UINT textureIndex;   // номер текстуры
		bool additive;   // рисовать аддитивно (свечение)
		UINT animFrames = 0;     // 0 = не анимирован
		UINT animCols = 1;       // колонок в листе
		UINT animRows = 1;       // рядов в листе
		float animFps = 0.0f; 
		float z = 0.0f; // кадров в секунду
	};

	struct Particle
	{
		float x, y;          // позиция
		float vx, vy;        // скорость
		float life;          // сколько осталось жить (сек)
		float maxLife;       // сколько жила изначально
		float size;
	};

	//Pipeline Value
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
	Microsoft::WRL::ComPtr<ID3D12Device> my_device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineStateGlow;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_depthBuffer;
	UINT m_rtvDescriptorHeapSize;
	UINT m_srvDescriptorSize = 0;

	//Textures
	Microsoft::WRL::ComPtr<ID3D12Resource> m_textures[TextureCount];

	//App Resource
	Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferViews;
	UINT m_vertexCount = 0;

	//Shapes
	std::vector<Shape> m_shapes;
	float m_time = 0.0f;

	//Particles
	std::vector<Particle> m_particles;
	float m_spawnAccum = 0.0f;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_particleBuffer;
	UINT8* m_particleMapped = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_particleView = {};
	UINT m_particleVertexCount = 0;

	//Sync Objects
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	std::vector<UINT8> GetTextureData(UINT type);

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitforPreviousFrame();
	void UpdateParticles(float dt);
};