#include "Window.h"
#include <stdexcept>
#include "Game.h"
HWND Window::hWnd = nullptr;
Window::Window()
{
}
LRESULT CALLBACK WinProc(HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM  lParam)
{
    Game* gameOne = reinterpret_cast<Game*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (uMessage)
    {
    case WM_CREATE:
    {
        LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam); SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
    }
    return 0;
    case WM_PAINT:
        if (gameOne)
        {
            gameOne->OnUpdate();
            gameOne->OnRender();
        }
        return 0;
    case WM_DESTROY:
    {
        Window* window = (Window*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        window->OnDestroy();
        ::PostQuitMessage(0);
        break;
    }
    case WM_SIZE:
    {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
    }
    case WM_MOUSEWHEEL:
    {
        IDX* pSample = reinterpret_cast<IDX*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        if (pSample)
            pSample->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
        return 0;
    }
    default:
        return ::DefWindowProcW(hWnd, uMessage, wParam, lParam);
    }

    return DefWindowProc(hWnd, uMessage, wParam, lParam);

}

int Window::Run(IDX* game, HINSTANCE hInstance, int cmdShow)
{
    hInstance = GetModuleHandle(nullptr);

    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WinProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WindowClass";
    RegisterClassEx(&wc);

    RECT r = { 0, 0, (LONG)game->GetWidth(), (LONG)game->GetHeight() };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    hWnd = CreateWindow(wc.lpszClassName, game->GetName(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInstance, game);

    game->OnInit();
    ShowWindow(hWnd, cmdShow);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            game->OnUpdate();
            game->OnRender();
        }
    }

    game->OnDestroy();
    return static_cast<int>(msg.wParam);
}

int  Window::Init()
{
	WNDCLASSEX  wc = {};
	wc.cbClsExtra = NULL;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.cbWndExtra = NULL;
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.hCursor = LoadCursor(NULL, IDC_CROSS);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	wc.hInstance = NULL;
	wc.lpszClassName = L"WindowClass";
	wc.lpszMenuName = L"";
	wc.style = NULL;
	wc.lpfnWndProc = &WinProc;
	
	if (!::RegisterClassEx(&wc))
	{
		return false;

	}

	hWnd = ::CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, L"WindowClass", L"ConsoleEngine", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1920, 1080, NULL, NULL, NULL, this);

	if (!hWnd)
	{
		throw std::runtime_error("Window::Init()  failed");
	}

	::ShowWindow(hWnd, SW_SHOW);
	::UpdateWindow(hWnd);

	isRunning = true;

	return true;
}

bool Window::Broadcast()
{
	MSG message;
	while (::PeekMessage(&message, NULL, 0, 0, PM_REMOVE) > 0)
	{
		TranslateMessage(&message);
		DispatchMessage(&message);
	}
	this->OnUpdate();
	Sleep(1);
	return true;
}

bool Window::Release()
{
	if (!::DestroyWindow(hWnd))
		return false;


	return true;
}

void Window::OnCreate()
{
}

void Window::OnUpdate()
{
}

void Window::OnDestroy()
{
	isRunning = false;
}

bool Window::isWindowRunning()
{
	return isRunning;
}

Window::~Window()
{
}
