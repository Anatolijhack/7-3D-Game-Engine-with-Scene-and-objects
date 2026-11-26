#include "AppWindow.h"
#include "Game.h"
#include "Game2.h"
#include "Game3.h"

int main()
{
	Game3 gameTriangle(1920, 1080, L"CosmoEngine");
	int CmdShow = SW_SHOW;
	HINSTANCE hInstance = nullptr;
	return Window::Run(&gameTriangle, hInstance, CmdShow);;
	/*AppWindow app;
	if (app.Init())
	{
		while (app.isWindowRunning())
		{
			app.Broadcast();
		}
	}
	return 0;*/
}