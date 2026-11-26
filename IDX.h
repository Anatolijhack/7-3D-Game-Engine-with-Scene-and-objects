#pragma once
#include <Windows.h>
#include <string>
#include <dxgi1_6.h>

class IDX
{
public:
	IDX(UINT width, UINT heigt, std::wstring name);
	virtual ~IDX();

	virtual void OnInit() = 0;
	virtual void OnUpdate() = 0;
	virtual void OnRender() = 0;
	virtual void OnDestroy() = 0;
	virtual void OnMouseWheel(float delta) {};
	
	UINT GetWidth() const { return m_weight; }
	UINT GetHeight() const { return m_height; }
	const WCHAR* GetName() const { return m_title.c_str(); }
protected:
	inline void GetAssetsPath(_Out_writes_(pathSize) WCHAR* path, UINT pathSize)
	{
		if (path == nullptr)
			throw std::exception();

		DWORD size = GetModuleFileName(nullptr, path, pathSize);
		if (size == 0 || size == pathSize)
			throw std::exception();   // ошибка или путь не поместился

		WCHAR* lastSlash = wcsrchr(path, L'\\');
		if (lastSlash)
			*(lastSlash + 1) = L'\0'; // отрезаем имя .exe
	}
	void GetHardwareAdapter(_In_ IDXGIFactory1* pFactory, _Outptr_opt_result_maybenull_ IDXGIAdapter1** ppAdapter,bool requestHighPerformanceAdapter = false);
	std::wstring GetAssettsFullPath(LPCWSTR assetname);
	UINT m_weight;
	UINT m_height;
	std::wstring m_assetsPath;
	std::wstring m_title;
	float aspecrt_ratio;
	bool m_useWarpAdapter;

	
};