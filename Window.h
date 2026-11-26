#pragma once
#include <Windows.h>
#include "IDX.h"


class Window
{
public:
	Window();
	static int  Run(IDX* idGame, HINSTANCE hInstance, int CmdShow);
	static HWND GetHwnd() { return hWnd; }
	int Init();
	bool Broadcast();
	bool Release();
	virtual void OnCreate();
	virtual void OnUpdate();
	virtual void OnDestroy();
	bool isWindowRunning();

	~Window();
protected:
	static HWND hWnd;
	bool isRunning = false;
};

