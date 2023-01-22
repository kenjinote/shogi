#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib,"d3d11")
#pragma comment(lib,"d2d1")
#pragma comment(lib,"dxgi")
#pragma comment(lib,"dxguid")

#include <windows.h>
#include <list>
#include <vector>
#include <random>

#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <d2d1svg.h>
#include <d2d1_3.h>
#include <d2d1_1helper.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include "resource.h"

#define CLIENT_WIDTH (960)
#define CLIENT_HEIGHT (600)
#define CARD_WIDTH (224.22508f)
#define CARD_HEIGHT (312.80777f)
#define CARD_SCALE (0.5f)
#define CARD_OFFSET (30.0f)
#define BOARD_OFFSET (10.0f)
#define NOT_FOUND (-1)
#define ANIMATION_TIME (250)

TCHAR szClassName[] = TEXT("shogi");

D3D_FEATURE_LEVEL featureLevels[] = {
	D3D_FEATURE_LEVEL_11_1,
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0,
	D3D_FEATURE_LEVEL_9_3,
	D3D_FEATURE_LEVEL_9_2,
	D3D_FEATURE_LEVEL_9_1,
};

template <typename T>
inline void SafeRelease(T*& p)
{
	if (NULL != p)
	{
		p->Release();
		p = NULL;
	}
}

double easeOutExpo(double t, double b, double c, double d)
{
	return (t == d) ? b + c : c * (-pow(2, -10 * t / d) + 1) + b;
}

HRESULT CreateSvgDocumentFromResource(_In_ LPCWSTR lpName, _In_ ID2D1DeviceContext6* d2dContext, _Out_ ID2D1SvgDocument** m_svgDocument)
{
	HMODULE hModule = GetModuleHandle(0);
	HRSRC hRes = FindResource(hModule, lpName, L"SVG");
	if (hRes == NULL)
	{
		return -1;
	}
	HGLOBAL hResLoad = LoadResource(hModule, hRes);
	if (hResLoad == NULL)
	{
		return -1;
	}
	int nSize = SizeofResource(hModule, hRes);
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, nSize);
	if (!hMem) {
		return -1;
	}
	LPVOID ptr = GlobalLock(hMem);
	if (!ptr) {
		return -1;
	}
	memcpy(ptr, LockResource(hResLoad), nSize);
	GlobalUnlock(hMem);
	IStream* pIStream = 0;
	if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pIStream))) {
		return -1;
	}
	if (FAILED(d2dContext->CreateSvgDocument(
		pIStream,
		D2D1::SizeF(1, 1),
		m_svgDocument
	))) {
		return -1;
	}
	return S_OK;
}

class Koma {
public:
	enum TYPE {
		NONE, FU, GIN, GYOKU, HISYA, KAKU, KEI, KIN, KYO, NARIGIN, NARIKEI, NARIKYO, OH, RYU, TO, UMA
	};
	~Koma() {
		SafeRelease(m_svgDocument);
	}
	ID2D1SvgDocument* m_svgDocument = NULL;
	TYPE no = NONE;
	BOOL bVisible = TRUE;
	BOOL bCanDrag = FALSE;
	BOOL bDrag = FALSE; // ドラッグ中はtrue
	float animation_from_x = 0;
	float animation_from_y = 0;
	float x = 0;
	float y = 0;
	float width = CARD_WIDTH;
	float height = CARD_HEIGHT;
	float scale = CARD_SCALE;
	float offset_x = 0;
	float offset_y = 0;
	ULONGLONG animation_start_time = 0;
	void Draw(ID2D1DeviceContext6* d2dDeviceContext, ID2D1Brush* brush) {
		if (!d2dDeviceContext) return;
		if (!bVisible) return;

		float xx;
		float yy;

		ULONGLONG now = GetTickCount64();
		if (now < animation_start_time) {
			xx = animation_from_x;
			yy = animation_from_y;
		}
		else if (now > animation_start_time + ANIMATION_TIME) {
			animation_start_time = 0;
			xx = x;
			yy = y;
		}
		else {
			xx = (float)(easeOutExpo((double)(now - animation_start_time), animation_from_x, x - animation_from_x, (double)(ANIMATION_TIME)));
			yy = (float)(easeOutExpo((double)(now - animation_start_time), animation_from_y, y - animation_from_y, (double)(ANIMATION_TIME)));
		}
		D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
		transform = transform * D2D1::Matrix3x2F::Scale(scale, scale);
		transform = transform * D2D1::Matrix3x2F::Translation(xx, yy);
		d2dDeviceContext->SetTransform(transform);
		d2dDeviceContext->DrawSvgDocument(m_svgDocument);
	}
	BOOL HitTest(float _x, float _y) {
		if (
			bVisible &&
			_x >= x &&
			_x <= x + scale * width &&
			_y >= y &&
			_y <= y + scale * height
			)
		{
			return TRUE;
		}
		return FALSE;
	}
	BOOL CanDrag() {
		if (bVisible && bCanDrag) {
			return TRUE;
		}
		return FALSE;
	}
};

class Masu {
public:
	float x = 0;
	float y = 0;
	enum TYPE {
		freecell,
		homecell,
		tablecell
	};
	TYPE type = tablecell;
	void push_back(Koma* c, ULONGLONG delay = 0ULL) {
		c->animation_start_time = GetTickCount64() + delay;
		c->animation_from_x = c->x;
		c->animation_from_y = c->y;
		if (type == freecell) {
			c->x = x;
			c->y = y;
		}
		else if (type == homecell) {
			c->x = x;
			c->y = y;
		}
		else {
			c->x = x;
			c->y = y + (cards.size()) * CARD_OFFSET;
		}
		cards.push_back(c);
	}
	bool canpush(int card_no) {
		if (type == freecell) {
			return (cards.size() == 0);
		}
		else if (type == homecell) {
			if (cards.size() == 0) {
				return (card_no % 100 == 1);
			}
			else {
				Koma* back = cards.back();
				return ((back->no / 100 == card_no / 100) && (back->no % 100 + 1 == card_no % 100));
			}
		}
		else {
			if (cards.size() == 0) {
				return true;
			}
			else {
				Koma* back = cards.back();
				return (((back->no / 100 + card_no / 100) % 2 == 1) && (back->no % 100 == card_no % 100 + 1));
			}
		}
		return false;
	}
	void clear() {
		cards.clear();
	}
	void SetCanDragCard() {
		for (auto& i : cards) {
			i->bCanDrag = FALSE;
		}
		if (type == freecell || type == homecell) {
			if (cards.size() > 0) {
				Koma* back = cards.back();
				back->bCanDrag = TRUE;
			}
		}
		else if (type == tablecell) {
			bool first = true;
			bool second = false;
			int prev = 0;
			for (auto i = cards.rbegin(), e = cards.rend(); i != e; ++i) {
				if (first) {
					(*i)->bCanDrag = TRUE;
					first = false;
					second = true;
					prev = (*i)->no;
					continue;
				}
				if (second) {
					if (prev % 100 + 1 == (*i)->no % 100 && (prev / 100 + (*i)->no / 100) % 2 == 1) {
						(*i)->bCanDrag = TRUE;
						prev = (*i)->no;
					}
					else {
						break;
					}
				}
			}
		}
	}
	void draw(ID2D1DeviceContext6* d2dDeviceContext, ID2D1SolidColorBrush* selectBrush, ID2D1SolidColorBrush* emptyBrush) {
		D2D1_RECT_F rect;
		rect.left = x;
		rect.top = y;
		rect.right = x + CARD_SCALE * CARD_WIDTH;
		rect.bottom = y + CARD_SCALE * CARD_HEIGHT;
		d2dDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
		d2dDeviceContext->FillRectangle(rect, emptyBrush);
		for (auto ii = cards.begin(), e = cards.end(); ii != e; ++ii) {
			if (!(*ii)->bDrag) {
				(*ii)->Draw(d2dDeviceContext, selectBrush);
			}
		}
	}
	void NormalizationPos() {
		if (type == freecell || type == homecell) {
			for (auto p : cards) {
				p->animation_start_time = GetTickCount64();
				p->animation_from_x = p->x;
				p->animation_from_y = p->y;
				p->x = x;
				p->y = y;
			}
		}
		else {
			int i = 0;
			for (auto p : cards) {
				p->animation_start_time = GetTickCount64();
				p->animation_from_x = p->x;
				p->animation_from_y = p->y;
				p->x = x;
				p->y = y + i * CARD_OFFSET;
				i++;
			}
		}
	}
	size_t size() {
		return cards.size();
	}
	void GetCardListFromPos(float _x, float _y, std::vector<Koma*>& dragcard) {
		dragcard.clear();
		for (auto it = cards.rbegin(), end = cards.rend(); it != end; ++it) {
			if ((*it)->HitTest(_x, _y) && (*it)->CanDrag()) {
				dragcard.push_back(*it);
				break;
			}
		}
		if (dragcard.size() > 0) {
			bool first = false;
			for (auto it = cards.begin(), end = cards.end(); it != end; ++it) {
				if (*it == dragcard[0]) {
					first = true;
					continue;
				}
				if (first) {
					dragcard.push_back(*it);
				}
			}
		}
	}
	void GetCardListFromCount(byte count, std::vector<Koma*>& dragcard) {
		dragcard.clear();
		int i = 0;
		for (auto it = cards.begin(), end = cards.end(); it != end; ++it) {
			if (cards.size() - count <= i) {
				dragcard.push_back(*it);
			}
			i++;
		}
	}
	void resize(size_t size) {
		cards.resize(size);
	}
	std::vector<Koma*> cards;
};

struct operation {
	byte from_board_no;
	byte to_board_no;
	byte card_count;
};

class Game {
public:
	ID2D1SvgDocument* m_svgBackground = NULL;
	ID2D1SolidColorBrush* selectBrush = NULL;
	std::list<Koma*> pkoma;
	std::vector<Koma*> dragcard;
	Masu masu[9 * 9];
	int from_board_no = 0;
	HWND hWnd;
	ULONGLONG animation_start_time;
	std::vector<operation> buffer;
	int generation = 0;
	Game(HWND hWnd, ID2D1DeviceContext6* d2dDeviceContext) {
		this->hWnd = hWnd;
		HRESULT hr = S_OK;
		if (SUCCEEDED(hr)) {
			hr = d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(1.0F, 1.0F, 1.0F, 0.5F), &selectBrush);
		}
		if (SUCCEEDED(hr)) {
			const int ids[] = {
				IDR_FU,IDR_GIN,IDR_GYOKU,IDR_HISYA,IDR_KAKU,IDR_KEI,IDR_KIN,IDR_KYO,IDR_NARIGIN,IDR_NARIKEI,IDR_NARIKYO,IDR_OH,IDR_RYU,IDR_TO,IDR_UMA
			};
			for (int i = 0; i < _countof(ids); i++) {
				Koma* p = new Koma;
				CreateSvgDocumentFromResource(MAKEINTRESOURCE(ids[i]), d2dDeviceContext, &p->m_svgDocument);
				pkoma.push_back(p);
			}
		}
		CreateSvgDocumentFromResource(MAKEINTRESOURCE(IDR_BACKGROUND_LIGHT), d2dDeviceContext, &m_svgBackground);
	}
	~Game() {
		for (auto& i : pkoma) {
			delete i;
			i = 0;
		}
		SafeRelease(selectBrush);
		SafeRelease(m_svgBackground);
	}
	void OnNewGame(unsigned int seed = -1) {
		UnDragAll();
		for (auto& i : masu) {
			i.clear();
		};
		generation = 0;
		buffer.clear();
		masu[0].push_back(pkoma.back());
		InvalidateRect(hWnd, 0, 0);
		UpdateWindow(hWnd);
		SetCanDragCard();
	}
	void UnDragAll() {
		for (auto& i : pkoma) {
			i->bDrag = FALSE;
			i->offset_x = 0.0f;
			i->offset_y = 0.0f;
		}
		dragcard.clear();
	}
	void SetCanDragCard() {
	}
	void DrawBoard(ID2D1DeviceContext6* d2dDeviceContext) {
		if (m_svgBackground) {
			D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
			transform = transform * D2D1::Matrix3x2F::Scale(0.5, 0.5);
			d2dDeviceContext->SetTransform(transform);
			d2dDeviceContext->DrawSvgDocument(m_svgBackground);
		}
	}
	void OnLButtonDoubleClick(int x, int y) {
	}
	void OnLButtonDown(int x, int y) {
	}
	void OnMouseMove(int x, int y) {
	}
	void OnLButtonUP(int, int) {
	}
	void AnimationStart(ULONGLONG delay = 0ULL) {
		animation_start_time = GetTickCount64() + delay;
		SetTimer(hWnd, 0x1234, 1, NULL);
	}
	void OnTimer() {
		ULONGLONG now = GetTickCount64();
		if (now > animation_start_time + ANIMATION_TIME * 2) {
			KillTimer(hWnd, 0x1234);
		}
		InvalidateRect(hWnd, 0, 0);
	}
	void Operation(operation& op) {
		if (generation == buffer.size()) {
			buffer.push_back(op);
		}
		else {
			buffer.resize(generation);
			buffer.push_back(op);
		}
		generation++;
	}
	void OnUndo() {
	}
	void OnRedo() {
	}
};

void CenterWindow(HWND hWnd)
{
	RECT rc, rc2;
	int	x, y;
	HWND hParent = GetParent(hWnd);
	if (hParent) {
		GetWindowRect(hParent, &rc);
	}
	else {
		SystemParametersInfo(SPI_GETWORKAREA, 0, &rc, 0);
	}
	GetWindowRect(hWnd, &rc2);
	x = ((rc.right - rc.left) - (rc2.right - rc2.left)) / 2 + rc.left;
	y = ((rc.bottom - rc.top) - (rc2.bottom - rc2.top)) / 2 + rc.top;
	SetWindowPos(hWnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
}

INT_PTR CALLBACK VersionDialogProc(HWND hDlg, unsigned msg, WPARAM wParam, LPARAM lParam)
{
	static Game* g;
	switch (msg)
	{
	case WM_INITDIALOG:
		g = (Game*)lParam;
		CenterWindow(hDlg);
		return TRUE;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		break;
	}
	return FALSE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static ID3D11Device* d3dDevice;
	static IDXGIDevice* dxgiDevice;
	static ID2D1Device6* d2dDevice;
	static ID2D1DeviceContext6* d2dDeviceContext;
	static IDXGIFactory2* dxgiFactory;
	static IDXGISwapChain1* dxgiSwapChain;
	static IDXGISurface* dxgiBackBufferSurface;
	static ID2D1Bitmap1* bmpTarget;
	static Game* g;
	switch (msg)
	{
	case WM_TIMER:
	{
		g->OnTimer();
		break;
	}
	case WM_LBUTTONDBLCLK:
	{
		int x = GET_X_LPARAM(lParam);
		int y = GET_Y_LPARAM(lParam);
		g->OnLButtonDoubleClick(x, y);
		break;
	}
	case WM_LBUTTONDOWN:
	{
		int x = GET_X_LPARAM(lParam);
		int y = GET_Y_LPARAM(lParam);
		g->OnLButtonDown(x, y);
		break;
	}
	case WM_MOUSEMOVE:
	{
		int x = GET_X_LPARAM(lParam);
		int y = GET_Y_LPARAM(lParam);
		g->OnMouseMove(x, y);
		break;
	}
	case WM_LBUTTONUP:
	{
		int x = GET_X_LPARAM(lParam);
		int y = GET_Y_LPARAM(lParam);
		g->OnLButtonUP(x, y);
		break;
	}
	case WM_CREATE:
	{
		HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels, 7, D3D11_SDK_VERSION, &d3dDevice, NULL, NULL);
		if (SUCCEEDED(hr)) {
			hr = d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
		}
		if (SUCCEEDED(hr)) {
			hr = D2D1CreateDevice(dxgiDevice, D2D1::CreationProperties(D2D1_THREADING_MODE_SINGLE_THREADED, D2D1_DEBUG_LEVEL_NONE, D2D1_DEVICE_CONTEXT_OPTIONS_NONE), (ID2D1Device**)&d2dDevice);
		}
		if (SUCCEEDED(hr)) {
			hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, (ID2D1DeviceContext**)&d2dDeviceContext);
		}
		if (SUCCEEDED(hr)) {
			hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, __uuidof(IDXGIFactory2), (void**)&dxgiFactory);
		}
		if (SUCCEEDED(hr)) {
			DXGI_SWAP_CHAIN_DESC1 dscd = {};
			dscd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
			dscd.BufferCount = 2;
			dscd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			dscd.Flags = 0;
			dscd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			dscd.Height = CLIENT_HEIGHT;
			dscd.SampleDesc.Count = 1;
			dscd.SampleDesc.Quality = 0;
			dscd.Scaling = DXGI_SCALING_NONE;
			dscd.Stereo = FALSE;
			dscd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			dscd.Width = CLIENT_WIDTH;
			hr = dxgiFactory->CreateSwapChainForHwnd(d3dDevice, hWnd, &dscd, NULL, NULL, &dxgiSwapChain);
		}
		if (SUCCEEDED(hr)) {
			hr = dxgiSwapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&dxgiBackBufferSurface);
		}
		if (SUCCEEDED(hr)) {
			hr = d2dDeviceContext->CreateBitmapFromDxgiSurface(dxgiBackBufferSurface, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 0, 0), &bmpTarget);
		}
		if (SUCCEEDED(hr)) {
			d2dDeviceContext->SetTarget(bmpTarget);
			d2dDeviceContext->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
			d2dDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
			d2dDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE::D2D1_ANTIALIAS_MODE_ALIASED);
			g = new Game(hWnd, d2dDeviceContext);
			if (!g) return -1;
//			PostMessage(hWnd, WM_COMMAND, ID_NEW_GAME, 0);
		}
		break;
	}
/*
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_NEW_GAME:
			g->OnNewGame();
			break;
		}
		break;
*/
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		if (BeginPaint(hWnd, &ps))
		{
			d2dDeviceContext->BeginDraw();
			d2dDeviceContext->Clear(D2D1::ColorF(D2D1::ColorF::ForestGreen));
			g->DrawBoard(d2dDeviceContext);
			d2dDeviceContext->EndDraw();
			dxgiSwapChain->Present(1, 0);
			EndPaint(hWnd, &ps);
		}
		break;
	}
	case WM_DESTROY:
		KillTimer(hWnd, 0x1234);
		SafeRelease(d3dDevice);
		SafeRelease(dxgiBackBufferSurface);
		SafeRelease(dxgiDevice);
		SafeRelease(dxgiFactory);
		SafeRelease(dxgiSwapChain);
		SafeRelease(d2dDevice);
		SafeRelease(d2dDeviceContext);
		SafeRelease(bmpTarget);
		delete g;
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd)
{
	HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	MSG msg = {};
	WNDCLASS wndclass = { CS_DBLCLKS, WndProc, 0, 0, hInstance, LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1)), LoadCursor(0,IDC_ARROW), 0, MAKEINTRESOURCE(IDR_MENU1), szClassName };
	RegisterClass(&wndclass);
	RECT rect = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
	DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	AdjustWindowRect(&rect, dwStyle, FALSE);
	HWND hWnd = CreateWindow(szClassName, L"shogi", dwStyle, CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, 0, 0, hInstance, 0);
	ShowWindow(hWnd, SW_SHOWDEFAULT);
	UpdateWindow(hWnd);
	HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR1));
	while (GetMessage(&msg, 0, 0, 0))
	{
		if (!TranslateAccelerator(hWnd, hAccel, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	DestroyAcceleratorTable(hAccel);
	CoUninitialize();
	return (int)msg.wParam;
}