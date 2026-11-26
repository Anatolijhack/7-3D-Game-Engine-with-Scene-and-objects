#pragma once
#include "IDX.h"
#include <wrl.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <DirectXMath.h>
class Game : public IDX
{
public:
	Game(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();
private:
	static const UINT FrameCount = 2;
	struct Vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 color;
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
	//App Resource
	Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer; 
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferViews;

	//Sync Objects
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;
	
	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitforPreviousFrame();
};