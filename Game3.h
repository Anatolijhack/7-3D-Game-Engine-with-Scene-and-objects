#pragma once
#include "IDX.h"
#include <wrl.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <DirectXMath.h>
#include <vector>

class Game3 : public IDX
{
public:
	Game3(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();
	virtual void OnMouseWheel(float delta);
	void StepPhysics(float dt);
private:

	static const UINT FrameCount = 2;
	struct Vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 color;
		DirectX::XMFLOAT2 uv = { 0.0f, 0.0f };
		DirectX::XMFLOAT3 normal = { 0.0f, 0.0f, 1.0f };
	};
	//Pipeline Value
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
	Microsoft::WRL::ComPtr<ID3D12Device> my_device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	UINT m_rtvDescriptorHeapSize;

	//Depth
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_depthBuffer;

	//App Resource
	Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferViews;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};

	struct Particle3D
	{
		DirectX::XMFLOAT3 pos, vel;
		float life, maxLife, size;
	};
	static const UINT MaxParticles = 1000;
	static const UINT MaxFxQuads = MaxParticles + 8;   // частицы + ореолы

	static const int MaxLights = 8;

	struct PointLight
	{
		DirectX::XMFLOAT3 pos;   float range;      // позиция + радиус действия
		DirectX::XMFLOAT3 color; float intensity;  // цвет + яркость
	};

	struct LightData
	{
		DirectX::XMFLOAT4 ambient;   // фоновый свет
		int   count;
		float pad[3];
		PointLight lights[MaxLights];
		DirectX::XMFLOAT4X4 lightVP;
	};

	// в классе:
	Microsoft::WRL::ComPtr<ID3D12Resource> m_lightBuffer;
	LightData* m_lightMapped = nullptr;

	struct Constants
	{
		DirectX::XMFLOAT4X4 mvp;
		DirectX::XMFLOAT4X4 world;
		float time;
		float pulse;
		float mode;      // 0 = объект с освещением, 1 = свечение
		float pad;
		DirectX::XMFLOAT4 lightDir;   // направление, В КОТОРОМ летит свет
		DirectX::XMFLOAT4 camPos;     // положение камеры (для блика)
	};
	Constants m_constants = {};

	DirectX::XMFLOAT4X4 m_worldCube, m_worldSphere, m_identity;
	float m_time = 0.0f;

	DirectX::XMFLOAT4X4 m_viewProj;
	DirectX::XMFLOAT3 m_camRight, m_camUp;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineStateGlow;

	std::vector<Particle3D> m_particles;
	float m_spawnAccum = 0.0f;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_fxBuffer;
	UINT8* m_fxMapped = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_fxView = {};
	UINT m_fxVertexCount = 0;

	void AddBillboard(std::vector<Vertex>& v, DirectX::XMFLOAT3 c, float size, DirectX::XMFLOAT4 col);
	void UpdateEffects(float dt);

	struct Mesh
	{
		UINT indexCount;    // сколько индексов у объекта
		UINT firstIndex;    // с какого индекса в общем буфере он начинается
		INT  baseVertex;    // с какой вершины в общем буфере он начинается
	};
	Mesh m_cube = {};
	Mesh m_sphere = {};
	float m_angle = 0.0f;
	DirectX::XMFLOAT4X4 m_mvpCube;
	DirectX::XMFLOAT4X4 m_mvpSphere;

	//Sync Objects
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	float m_yaw = 0.0f;
	float m_pitch = 0.2f;       // чуть сверху
	float m_radius = 4.0f;      // то же расстояние, что было (-4)
	bool m_dragging = false;
	POINT m_lastMouse = {};


	static const UINT TextureWidth = 256;
	static const UINT TextureHeight = 256;
	static const UINT TexturePixelSize = 4;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_texture;

	struct SceneObject
	{
		int   meshId;      // 0 = куб, 1 = шар, 2 = пол
		DirectX::XMFLOAT3 pos;
		float scale;
		float spin;        // скорость вращения вокруг Y (множитель m_angle)
		float tilt;        // вращение вокруг X
		float pulse;  // пульсация размера в вершинном шейдере
		bool  dynamic = false;                   // двигает ли его физика
		float restitution = 0.6f;                // упругость: 0 глина, 1 идеальный отскок
		DirectX::XMFLOAT3 vel = { 0, 0, 0 };     // скорость
		DirectX::XMFLOAT4X4 mvp, world;   // считаются в OnUpdate
		DirectX::XMFLOAT4X4 lightMvp;
	};
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_shadowMap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_shadowDsvHeap;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_pipelineStateShadow;
	D3D12_VIEWPORT m_shadowViewport;
	D3D12_RECT     m_shadowScissor;
	// в классе:
	std::vector<SceneObject> m_objects;
	decltype(m_cube) m_floor;   // тот же тип, что у m_cube и m_sphere
	static const UINT ShadowMapSize = 2048;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineStateSky;
	DirectX::XMFLOAT4X4 m_invViewProj;
	std::vector<UINT8> GetTextureData();
	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitforPreviousFrame();
};