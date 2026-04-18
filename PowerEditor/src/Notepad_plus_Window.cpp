// This file is part of Notepad++ project
// Copyright (C)2021 Don HO <don.h@free.fr>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include <shlwapi.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <fstream>
#include "Notepad_plus_Window.h"
#include "menuCmdID.h"

HWND Notepad_plus_Window::gNppHWND = NULL;

namespace // anonymous
{

	struct PaintLocker final
	{
		explicit PaintLocker(HWND handle)
			: handle(handle)
		{
			// disallow drawing on the window
			LockWindowUpdate(handle);
		}

		~PaintLocker()
		{
			// re-allow drawing for the window
			LockWindowUpdate(NULL);

			// force re-draw
			InvalidateRect(handle, nullptr, TRUE);
			RedrawWindow(handle, nullptr, NULL, RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_INVALIDATE);
		}

		HWND handle;
	};

} // anonymous namespace

using namespace std;

void Notepad_plus_Window::setStartupBgColor(COLORREF BgColor)
{
	RECT windowClientArea;
	HDC hdc = GetDCEx(_hSelf, NULL, DCX_CACHE | DCX_LOCKWINDOWUPDATE); //lock window update flag due to PaintLocker
	GetClientRect(_hSelf, &windowClientArea);
	HBRUSH hBrush = ::CreateSolidBrush(BgColor);
	::FillRect(hdc, &windowClientArea, hBrush);
	::DeleteObject(hBrush);
	ReleaseDC(_hSelf, hdc);
}


// ===== RE2 Start Center: owner-painted tile dialog =====
namespace re2 {

constexpr int IDD_RE2_STARTCENTER = 27000;
constexpr int kTileCount = 3;
constexpr int kDialogW = 720;
constexpr int kDialogH = 480;
constexpr int kRailW = 200;
constexpr int kTileTop = 80;
constexpr int kTileH = 320;
constexpr int kTilePad = 20;

constexpr COLORREF BG        = RGB(0x07, 0x15, 0x24);
constexpr COLORREF RAIL      = RGB(0x00, 0x0C, 0x18);
constexpr COLORREF TILE_BG   = RGB(0x0B, 0x1E, 0x32);
constexpr COLORREF TILE_HOV  = RGB(0x18, 0x38, 0x5F);
constexpr COLORREF ACCENT    = RGB(0x66, 0x88, 0xCC);
constexpr COLORREF TEXT_HI   = RGB(0xE6, 0xEE, 0xF8);
constexpr COLORREF TEXT_LO   = RGB(0x88, 0xA0, 0xC0);

struct TileSpec {
	int id;
	const wchar_t* title;
	const wchar_t* desc;
	int iconKind; // 0=page 1=brackets 2=folder
};

static const TileSpec kTiles[kTileCount] = {
	{ 1001, L"Blank Document",  L"Start with an empty file.",                              0 },
	{ 1002, L"Blank HTML",      L"New file pre-filled with HTML5 boilerplate.",            1 },
	{ 1003, L"Web Project",     L"Creates index.html, style.css, script.js in a folder.",  2 },
};

struct DlgState {
	int hoveredTile = -1;
	bool tracking = false;
};

static RECT getTileRect(int i)
{
	int avail = kDialogW - kRailW - kTilePad * (kTileCount + 1);
	int tileW = avail / kTileCount;
	RECT r{};
	r.left = kRailW + kTilePad + i * (tileW + kTilePad);
	r.top = kTileTop;
	r.right = r.left + tileW;
	r.bottom = r.top + kTileH;
	return r;
}

static int hitTestTile(int x, int y)
{
	POINT p{ x, y };
	for (int i = 0; i < kTileCount; ++i)
	{
		RECT r = getTileRect(i);
		if (PtInRect(&r, p)) return i;
	}
	return -1;
}

static void drawIcon(HDC hdc, int kind, int cx, int cy)
{
	HPEN pen = CreatePen(PS_SOLID, 3, ACCENT);
	HPEN old = (HPEN)SelectObject(hdc, pen);
	HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
	HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, nullBrush);
	SetBkMode(hdc, TRANSPARENT);

	switch (kind)
	{
		case 0: // page: rounded rect + a few lines
			RoundRect(hdc, cx - 28, cy - 36, cx + 28, cy + 36, 8, 8);
			{
				HPEN line = CreatePen(PS_SOLID, 2, TEXT_LO);
				SelectObject(hdc, line);
				for (int i = 0; i < 4; ++i)
				{
					int y = cy - 20 + i * 12;
					MoveToEx(hdc, cx - 18, y, nullptr);
					LineTo(hdc, cx + 18, y);
				}
				SelectObject(hdc, pen);
				DeleteObject(line);
			}
			break;
		case 1: // </> brackets
			{
				SetTextColor(hdc, ACCENT);
				HFONT font = CreateFontW(56, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
					DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					CLEARTYPE_QUALITY, FF_DONTCARE, L"Cascadia Code");
				HFONT oldF = (HFONT)SelectObject(hdc, font);
				RECT r{ cx - 60, cy - 30, cx + 60, cy + 30 };
				DrawTextW(hdc, L"</>", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				SelectObject(hdc, oldF);
				DeleteObject(font);
			}
			break;
		case 2: // folder + 3 colored dots for HTML/CSS/JS
			{
				RECT f{ cx - 36, cy - 20, cx + 36, cy + 24 };
				Rectangle(hdc, f.left, f.top, f.right, f.bottom);
				// tab
				Rectangle(hdc, f.left + 4, f.top - 6, f.left + 24, f.top + 2);
				DeleteObject(pen);
				HBRUSH html = CreateSolidBrush(RGB(0xE3, 0x4C, 0x26));
				HBRUSH css  = CreateSolidBrush(RGB(0x26, 0x3E, 0xE3));
				HBRUSH js   = CreateSolidBrush(RGB(0xF7, 0xDF, 0x1E));
				HBRUSH oldB = (HBRUSH)SelectObject(hdc, html);
				pen = CreatePen(PS_NULL, 0, 0);
				HPEN oldP = (HPEN)SelectObject(hdc, pen);
				Ellipse(hdc, cx - 22, cy + 32, cx - 10, cy + 44);
				SelectObject(hdc, css);  Ellipse(hdc, cx - 6,  cy + 32, cx + 6,  cy + 44);
				SelectObject(hdc, js);   Ellipse(hdc, cx + 10, cy + 32, cx + 22, cy + 44);
				SelectObject(hdc, oldB); SelectObject(hdc, oldP);
				DeleteObject(html); DeleteObject(css); DeleteObject(js);
			}
			break;
	}

	SelectObject(hdc, old);
	SelectObject(hdc, oldBrush);
	DeleteObject(pen);
}

static void paintDialog(HWND hDlg, const DlgState& state)
{
	PAINTSTRUCT ps;
	HDC hdcScreen = BeginPaint(hDlg, &ps);
	RECT rc;
	GetClientRect(hDlg, &rc);

	// double buffer
	HDC hdc = CreateCompatibleDC(hdcScreen);
	HBITMAP bmp = CreateCompatibleBitmap(hdcScreen, rc.right, rc.bottom);
	HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, bmp);

	// background
	HBRUSH bgBrush = CreateSolidBrush(BG);
	FillRect(hdc, &rc, bgBrush);
	DeleteObject(bgBrush);

	// left rail
	RECT railRc{ 0, 0, kRailW, rc.bottom };
	HBRUSH railBrush = CreateSolidBrush(RAIL);
	FillRect(hdc, &railRc, railBrush);
	DeleteObject(railBrush);

	SetBkMode(hdc, TRANSPARENT);

	// Title in rail
	{
		HFONT titleFont = CreateFontW(28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
		HFONT oldF = (HFONT)SelectObject(hdc, titleFont);
		SetTextColor(hdc, TEXT_HI);
		RECT tRc{ 24, 48, kRailW - 16, 90 };
		DrawTextW(hdc, L"Notepad++", -1, &tRc, DT_LEFT | DT_SINGLELINE);
		tRc.top += 30; tRc.bottom += 30;
		SetTextColor(hdc, ACCENT);
		DrawTextW(hdc, L"RE2", -1, &tRc, DT_LEFT | DT_SINGLELINE);
		SelectObject(hdc, oldF);
		DeleteObject(titleFont);
	}
	// Subtitle
	{
		HFONT subFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
		HFONT oldF = (HFONT)SelectObject(hdc, subFont);
		SetTextColor(hdc, TEXT_LO);
		RECT sRc{ 24, 132, kRailW - 16, 160 };
		DrawTextW(hdc, L"dark. minimal. yours.", -1, &sRc, DT_LEFT | DT_SINGLELINE);

		// Footer
		RECT fRc{ 24, rc.bottom - 40, kRailW - 16, rc.bottom - 16 };
		SetTextColor(hdc, TEXT_LO);
		DrawTextW(hdc, L"Press Esc to skip.", -1, &fRc, DT_LEFT | DT_SINGLELINE);

		SelectObject(hdc, oldF);
		DeleteObject(subFont);
	}

	// "New" header
	{
		HFONT hFont = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
		HFONT oldF = (HFONT)SelectObject(hdc, hFont);
		SetTextColor(hdc, TEXT_HI);
		RECT hRc{ kRailW + kTilePad, 30, kDialogW - 20, 70 };
		DrawTextW(hdc, L"New", -1, &hRc, DT_LEFT | DT_SINGLELINE);
		SelectObject(hdc, oldF);
		DeleteObject(hFont);
	}

	// Tiles
	for (int i = 0; i < kTileCount; ++i)
	{
		RECT tr = getTileRect(i);
		bool hovered = (state.hoveredTile == i);

		HBRUSH tileBrush = CreateSolidBrush(hovered ? TILE_HOV : TILE_BG);
		HPEN tilePen = CreatePen(PS_SOLID, 1, hovered ? ACCENT : RGB(0x18, 0x2A, 0x42));
		HBRUSH oldB = (HBRUSH)SelectObject(hdc, tileBrush);
		HPEN oldP = (HPEN)SelectObject(hdc, tilePen);
		RoundRect(hdc, tr.left, tr.top, tr.right, tr.bottom, 14, 14);
		SelectObject(hdc, oldB); SelectObject(hdc, oldP);
		DeleteObject(tileBrush); DeleteObject(tilePen);

		int cx = (tr.left + tr.right) / 2;
		int cy = tr.top + 110;
		drawIcon(hdc, kTiles[i].iconKind, cx, cy);

		// title
		HFONT titleF = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
		HFONT oldF = (HFONT)SelectObject(hdc, titleF);
		SetTextColor(hdc, TEXT_HI);
		RECT titleRc{ tr.left + 16, tr.top + 190, tr.right - 16, tr.top + 220 };
		DrawTextW(hdc, kTiles[i].title, -1, &titleRc, DT_CENTER | DT_SINGLELINE);
		SelectObject(hdc, oldF);
		DeleteObject(titleF);

		// desc
		HFONT descF = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
		oldF = (HFONT)SelectObject(hdc, descF);
		SetTextColor(hdc, TEXT_LO);
		RECT descRc{ tr.left + 14, tr.top + 230, tr.right - 14, tr.bottom - 16 };
		DrawTextW(hdc, kTiles[i].desc, -1, &descRc, DT_CENTER | DT_WORDBREAK);
		SelectObject(hdc, oldF);
		DeleteObject(descF);
	}

	BitBlt(hdcScreen, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
	SelectObject(hdc, oldBmp);
	DeleteObject(bmp);
	DeleteDC(hdc);
	EndPaint(hDlg, &ps);
}

static INT_PTR CALLBACK startCenterProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
	DlgState* state = reinterpret_cast<DlgState*>(GetWindowLongPtr(hDlg, DWLP_USER));

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			state = new DlgState();
			SetWindowLongPtr(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
			// Resize to pixel dimensions (ignore DLU from template)
			int sw = GetSystemMetrics(SM_CXSCREEN);
			int sh = GetSystemMetrics(SM_CYSCREEN);
			RECT wr{ 0, 0, kDialogW, kDialogH };
			AdjustWindowRect(&wr, static_cast<DWORD>(GetWindowLongPtr(hDlg, GWL_STYLE)), FALSE);
			int w = wr.right - wr.left;
			int h = wr.bottom - wr.top;
			SetWindowPos(hDlg, nullptr, (sw - w) / 2, (sh - h) / 2, w, h, SWP_NOZORDER);
			return TRUE;
		}
		case WM_MOUSEMOVE:
		{
			if (!state) break;
			int x = GET_X_LPARAM(lp);
			int y = GET_Y_LPARAM(lp);
			int h = hitTestTile(x, y);
			if (h != state->hoveredTile)
			{
				state->hoveredTile = h;
				InvalidateRect(hDlg, nullptr, FALSE);
			}
			if (!state->tracking)
			{
				TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hDlg, 0 };
				TrackMouseEvent(&tme);
				state->tracking = true;
			}
			return TRUE;
		}
		case WM_MOUSELEAVE:
		{
			if (state) { state->hoveredTile = -1; state->tracking = false; InvalidateRect(hDlg, nullptr, FALSE); }
			return TRUE;
		}
		case WM_LBUTTONDOWN:
		{
			int h = hitTestTile(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
			if (h >= 0) EndDialog(hDlg, kTiles[h].id);
			return TRUE;
		}
		case WM_PAINT:
			if (state) paintDialog(hDlg, *state);
			return TRUE;
		case WM_ERASEBKGND:
			return TRUE; // handled in WM_PAINT via double buffer
		case WM_SETCURSOR:
		{
			POINT p; GetCursorPos(&p); ScreenToClient(hDlg, &p);
			SetCursor(LoadCursor(nullptr, hitTestTile(p.x, p.y) >= 0 ? IDC_HAND : IDC_ARROW));
			return TRUE;
		}
		case WM_KEYDOWN:
			if (wp == VK_ESCAPE) EndDialog(hDlg, 0);
			return TRUE;
		case WM_CLOSE:
			EndDialog(hDlg, 0);
			return TRUE;
		case WM_NCDESTROY:
			delete state;
			SetWindowLongPtr(hDlg, DWLP_USER, 0);
			return TRUE;
	}
	return FALSE;
}

} // namespace re2

void Notepad_plus_Window::showStartCenterRE2()
{
	INT_PTR result = DialogBoxParam(_hInst, MAKEINTRESOURCE(re2::IDD_RE2_STARTCENTER),
		_hSelf, re2::startCenterProc, 0);
	switch (result)
	{
		case 1002: createBlankHtmlRE2(); break;
		case 1003: createWebProjectRE2(); break;
		default: break;
	}
}

void Notepad_plus_Window::createBlankHtmlRE2()
{
	_notepad_plus_plus_core.fileNew();
	::SendMessage(_hSelf, WM_COMMAND, IDM_LANG_HTML, 0);
	static constexpr const char* html =
		"<!DOCTYPE html>\r\n"
		"<html lang=\"en\">\r\n"
		"<head>\r\n"
		"\t<meta charset=\"UTF-8\">\r\n"
		"\t<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\r\n"
		"\t<title>New Page</title>\r\n"
		"</head>\r\n"
		"<body>\r\n"
		"\t\r\n"
		"</body>\r\n"
		"</html>\r\n";
	_notepad_plus_plus_core._pEditView->execute(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(html));
}

void Notepad_plus_Window::createWebProjectRE2()
{
	IFileDialog* pfd = nullptr;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pfd))))
		return;

	DWORD flags = 0;
	pfd->GetOptions(&flags);
	pfd->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
	pfd->SetTitle(L"Choose a folder for your new web project");

	if (pfd->Show(_hSelf) != S_OK) { pfd->Release(); return; }

	IShellItem* item = nullptr;
	if (FAILED(pfd->GetResult(&item))) { pfd->Release(); return; }

	PWSTR folderPath = nullptr;
	if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &folderPath)))
	{
		item->Release(); pfd->Release(); return;
	}

	auto writeFile = [](const std::wstring& path, const std::string& content) {
		std::ofstream out(path, std::ios::binary);
		if (out) out.write(content.data(), content.size());
	};

	std::wstring base(folderPath);
	writeFile(base + L"\\index.html",
		"<!DOCTYPE html>\r\n<html lang=\"en\">\r\n<head>\r\n"
		"\t<meta charset=\"UTF-8\">\r\n\t<title>New Project</title>\r\n"
		"\t<link rel=\"stylesheet\" href=\"style.css\">\r\n</head>\r\n<body>\r\n"
		"\t<h1>Hello, world.</h1>\r\n\t<script src=\"script.js\"></script>\r\n"
		"</body>\r\n</html>\r\n");
	writeFile(base + L"\\style.css",
		"body {\r\n\tfont-family: 'Cascadia Code', monospace;\r\n"
		"\tbackground: #000C18;\r\n\tcolor: #6688CC;\r\n"
		"\tmargin: 2rem;\r\n}\r\n");
	writeFile(base + L"\\script.js",
		"document.addEventListener('DOMContentLoaded', () => {\r\n"
		"\tconsole.log('RE2 web project ready');\r\n});\r\n");

	std::wstring indexPath = base + L"\\index.html";
	BufferID bid = _notepad_plus_plus_core.doOpen(indexPath);
	if (bid != BUFFER_INVALID)
		_notepad_plus_plus_core.switchToFile(bid);

	CoTaskMemFree(folderPath);
	item->Release();
	pfd->Release();
}

void Notepad_plus_Window::init(HINSTANCE hInst, HWND parent, const wchar_t *cmdLine, CmdLineParams *cmdLineParams)
{
	Window::init(hInst, parent);
	WNDCLASS nppClass{};

	nppClass.style = CS_BYTEALIGNWINDOW | CS_DBLCLKS;
	nppClass.lpfnWndProc = Notepad_plus_Proc;
	nppClass.cbClsExtra = 0;
	nppClass.cbWndExtra = 0;
	nppClass.hInstance = _hInst;
	nppClass.hIcon = ::LoadIcon(hInst, MAKEINTRESOURCE(IDI_M30ICON));
	nppClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	nppClass.hbrBackground = ::CreateSolidBrush(::GetSysColor(COLOR_MENU));
	nppClass.lpszMenuName = MAKEINTRESOURCE(IDR_M30_MENU);
	nppClass.lpszClassName = _className;

	_isPrelaunch = cmdLineParams->_isPreLaunch;

	if (!::RegisterClass(&nppClass))
	{
		throw std::runtime_error("Notepad_plus_Window::init : RegisterClass() function failed");
	}

	NppParameters& nppParams = NppParameters::getInstance();
	NppGUI & nppGUI = nppParams.getNppGUI();

	if (cmdLineParams->_isNoPlugin)
		_notepad_plus_plus_core._pluginsManager.disable();

	nppGUI._isCmdlineNosessionActivated = cmdLineParams->_isNoSession;
	nppGUI._isFullReadOnly = cmdLineParams->_isFullReadOnly;
	nppGUI._isFullReadOnlySavingForbidden = cmdLineParams->_isFullReadOnlySavingForbidden;

	_hIconAbsent = ::LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICONABSENT));

	_hSelf = ::CreateWindowEx(
		WS_EX_ACCEPTFILES | (_notepad_plus_plus_core._nativeLangSpeaker.isRTL() ? WS_EX_LAYOUTRTL : 0),
		_className,
		L"Notepad++ RE2",
		(WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN),
		// CreateWindowEx bug : set all 0 to walk around the problem
		0, 0, 0, 0,
		_hParent, nullptr, _hInst,
		(LPVOID) this); // pass the ptr of this instantiated object
        // for retrieve it in Notepad_plus_Proc from
        // the CREATESTRUCT.lpCreateParams afterward.

	if (NULL == _hSelf)
		throw std::runtime_error("Notepad_plus_Window::init : CreateWindowEx() function return null");

	if (!cmdLineParams->_pluginMessage.empty())
	{
		SCNotification scnN{};
		scnN.nmhdr.code = NPPN_CMDLINEPLUGINMSG;
		scnN.nmhdr.hwndFrom = _hSelf;
		scnN.nmhdr.idFrom = reinterpret_cast<uptr_t>(cmdLineParams->_pluginMessage.c_str());
		_notepad_plus_plus_core._pluginsManager.notify(&scnN);
	}

	PaintLocker paintLocker{_hSelf};

	_notepad_plus_plus_core.staticCheckMenuAndTB();

	gNppHWND = _hSelf;

	if (cmdLineParams->isPointValid())
	{
		::MoveWindow(_hSelf, cmdLineParams->_point.x, cmdLineParams->_point.y, nppGUI._appPos.right, nppGUI._appPos.bottom, TRUE);
	}
	else
	{
		WINDOWPLACEMENT posInfo{};
		posInfo.length = sizeof(WINDOWPLACEMENT);
		posInfo.flags = 0;
		if (_isPrelaunch)
			posInfo.showCmd = SW_HIDE;
		else
			posInfo.showCmd = nppGUI._isMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;

		posInfo.ptMinPosition.x = (LONG)-1;
		posInfo.ptMinPosition.y = (LONG)-1;
		posInfo.ptMaxPosition.x = (LONG)-1;
		posInfo.ptMaxPosition.y = (LONG)-1;
		posInfo.rcNormalPosition.left   = nppGUI._appPos.left;
		posInfo.rcNormalPosition.top    = nppGUI._appPos.top;
		posInfo.rcNormalPosition.bottom = nppGUI._appPos.top + nppGUI._appPos.bottom;
		posInfo.rcNormalPosition.right  = nppGUI._appPos.left + nppGUI._appPos.right;

		//SetWindowPlacement will take care of situations, where saved position was in no longer available monitor
		::SetWindowPlacement(_hSelf,&posInfo);
		
		if (NppDarkMode::isEnabled())
			setStartupBgColor(NppDarkMode::getDlgBackgroundColor()); //draw dark background when opening Npp without position data
	}

	if ((nppGUI._tabStatus & TAB_MULTILINE) != 0)
		::SendMessage(_hSelf, NPPM_INTERNAL_MULTILINETABBAR, 0, 0);

	if (!nppGUI._menuBarShow)
		::SetMenu(_hSelf, NULL);

	if (cmdLineParams->_isNoTab || (nppGUI._tabStatus & TAB_HIDE))
	{
		const int tabStatusOld = nppGUI._tabStatus;
		::SendMessage(_hSelf, NPPM_HIDETABBAR, 0, TRUE);
		if (cmdLineParams->_isNoTab)
		{
			// Restore old settings when tab bar has been hidden from tab bar.
			if (!(tabStatusOld & TAB_HIDE))
				nppGUI._forceTabbarVisible = true;
		}
	}

	if (cmdLineParams->_alwaysOnTop)
		::SendMessage(_hSelf, WM_COMMAND, IDM_VIEW_ALWAYSONTOP, 0);

	std::chrono::steady_clock::duration sessionLoadingTime{};
	if (nppGUI._rememberLastSession && !nppGUI._isCmdlineNosessionActivated)
	{
		std::chrono::steady_clock::time_point sessionLoadingStartTP = std::chrono::steady_clock::now();
		_notepad_plus_plus_core.loadLastSession();
		sessionLoadingTime = std::chrono::steady_clock::now() - sessionLoadingStartTP;
	}

	if (nppParams.doFunctionListExport() || nppParams.doPrintAndExit())
	{
		::ShowWindow(_hSelf, SW_HIDE);
	}
	else if (!cmdLineParams->_isPreLaunch)
	{
		if (cmdLineParams->isPointValid())
			::ShowWindow(_hSelf, SW_SHOW);
		else
			::ShowWindow(_hSelf, nppGUI._isMaximized ? SW_MAXIMIZE : SW_SHOW);
	}
	else
	{
		HICON icon = nullptr;
		loadTrayIcon(_hInst, &icon);
		_notepad_plus_plus_core._pTrayIco = new trayIconControler(_hSelf, IDI_M30ICON, NPPM_INTERNAL_MINIMIZED_TRAY, icon, L"");
		_notepad_plus_plus_core._pTrayIco->doTrayIcon(ADD);
	}

	if(cmdLineParams->isPointValid() && NppDarkMode::isEnabled())
		setStartupBgColor(NppDarkMode::getDlgBackgroundColor()); //draw dark background when opening Npp through cmd with position data

	std::vector<std::wstring> fileNames;
	std::vector<std::wstring> patterns;
	patterns.push_back(L"*.xml");

	std::wstring nppDir = nppParams.getNppPath();

	LocalizationSwitcher & localizationSwitcher = nppParams.getLocalizationSwitcher();
	std::wstring localizationDir = nppDir;
	pathAppend(localizationDir, L"localization\\");

	_notepad_plus_plus_core.getMatchedFileNames(localizationDir.c_str(), 0, patterns, fileNames, false, false);
	for (const auto& fileName : fileNames)
		localizationSwitcher.addLanguageFromXml(fileName);

	fileNames.clear();
	ThemeSwitcher & themeSwitcher = nppParams.getThemeSwitcher();

	//  Get themes from both npp install themes dir and user data themes dir (AppData, settingsDir, or cloud) with the per user
	//  overriding default themes of the same name.

	std::wstring userDataThemeDir = nppParams.getUserPath(); // getUserPath will always pick the right settingsDir/Cloud/AppData location
	if (!userDataThemeDir.empty() && userDataThemeDir != nppDir)	
	{
		// append files from userDataThemeDir, unless it matches nppDir (which means the themes are already in the internal structure)
		pathAppend(userDataThemeDir, L"themes\\");
		_notepad_plus_plus_core.getMatchedFileNames(userDataThemeDir.c_str(), 0, patterns, fileNames, false, false);
		for (const auto& fileName: fileNames)
		{
			themeSwitcher.addThemeFromXml(fileName);
		}
	}

	fileNames.clear();

	std::wstring nppThemeDir = nppDir; // <- should use the pointer to avoid the constructor of copy
	pathAppend(nppThemeDir, L"themes\\");

	// Set theme directory to their installation directory
	themeSwitcher.setThemeDirPath(nppThemeDir);

	_notepad_plus_plus_core.getMatchedFileNames(nppThemeDir.c_str(), 0, patterns, fileNames, false, false);
	for (const auto& fileName : fileNames)
	{
		std::wstring themeName( themeSwitcher.getThemeFromXmlFileName(fileName.c_str()) );
		if (!themeSwitcher.themeNameExists(themeName.c_str()))
		{
			themeSwitcher.addThemeFromXml(fileName);
			
			if (!userDataThemeDir.empty() && userDataThemeDir != nppDir)
			{
				std::wstring userDataThemePath = userDataThemeDir;

				if (!doesDirectoryExist(userDataThemePath.c_str()))
				{
					::CreateDirectory(userDataThemePath.c_str(), NULL);
				}

				wchar_t* fn = PathFindFileName(fileName.c_str());
				pathAppend(userDataThemePath, fn);
				themeSwitcher.addThemeStylerSavePath(fileName, userDataThemePath);
			}
		}
	}

	if (NppDarkMode::isWindowsModeEnabled())
	{
		std::wstring themePath;
		std::wstring xmlFileName = NppDarkMode::getThemeName();
		if (!xmlFileName.empty())
		{
			if (!nppParams.isLocal() || nppParams.isCloud())
			{
				themePath = nppParams.getUserPath();
				pathAppend(themePath, L"themes\\");
				pathAppend(themePath, xmlFileName);
			}

			if (themePath.empty() || !doesFileExist(themePath.c_str()))
			{
				themePath = themeSwitcher.getThemeDirPath();
				pathAppend(themePath, xmlFileName);
			}
		}
		else
		{
			const auto& themeInfo = themeSwitcher.getElementFromIndex(0);
			themePath = themeInfo.second;
		}

		if (doesFileExist(themePath.c_str()))
		{
			nppGUI._themeName.assign(themePath);
			nppParams.reloadStylers(themePath.c_str());
			::SendMessage(_hSelf, WM_UPDATESCINTILLAS, TRUE, 0);
		}
	}

	// Restore all dockable panels from the last session
	for (size_t i = 0, len = _notepad_plus_plus_core._internalFuncIDs.size() ; i < len ; ++i)
		::SendMessage(_hSelf, WM_COMMAND, _notepad_plus_plus_core._internalFuncIDs[i], 0);

	std::chrono::steady_clock::duration cmdlineParamsLoadingTime{};
	std::vector<std::wstring> fns;
	if (cmdLine)
	{
		std::chrono::steady_clock::time_point cmdlineParamsLoadingStartTP = std::chrono::steady_clock::now();
		fns = _notepad_plus_plus_core.loadCommandlineParams(cmdLine, cmdLineParams);
		cmdlineParamsLoadingTime = std::chrono::steady_clock::now() - cmdlineParamsLoadingStartTP;
	}

	// Launch folder as workspace after all this dockable panel being restored from the last session
	// To avoid dockable panel toggle problem.
	if (cmdLineParams->_openFoldersAsWorkspace)
	{
		std::wstring emptyStr;
		_notepad_plus_plus_core.launchFileBrowser(fns, emptyStr, true);
	}
	::SendMessage(_hSelf, WM_ACTIVATE, WA_ACTIVE, 0);

	::SendMessage(_hSelf, NPPM_INTERNAL_CRLFFORMCHANGED, 0, 0);

	::SendMessage(_hSelf, NPPM_INTERNAL_NPCFORMCHANGED, 0, 0);

	::SendMessage(_hSelf, NPPM_INTERNAL_ENABLECHANGEHISTORY, 0, 0);

	::SendMessage(_hSelf, NPPM_INTERNAL_LINECUTCOPYWITHOUTSELECTION, 0, 0);

	::SendMessage(_hSelf, NPPM_INTERNAL_DISABLESELECTEDTEXTDRAGDROP, 0, 0);

	if (nppGUI._newDocDefaultSettings._addNewDocumentOnStartup && nppGUI._rememberLastSession)
	{
		::SendMessage(_hSelf, WM_COMMAND, IDM_FILE_NEW, 0);
	}

	// Notify plugins that Notepad++ is ready
	SCNotification scnN{};
	scnN.nmhdr.code = NPPN_READY;
	scnN.nmhdr.hwndFrom = _hSelf;
	scnN.nmhdr.idFrom = 0;
	_notepad_plus_plus_core._pluginsManager.notify(&scnN);

	if (!cmdLineParams->_easterEggName.empty())
	{
		if (cmdLineParams->_quoteType == 0) // Easter Egg Name
		{
			int iQuote = _notepad_plus_plus_core.getQuoteIndexFrom(cmdLineParams->_easterEggName.c_str());
			if (iQuote != -1)
			{
				_notepad_plus_plus_core.showQuoteFromIndex(iQuote);
			}
		}
		else if (cmdLineParams->_quoteType == 1) // command line quote
		{
			_userQuote = cmdLineParams->_easterEggName;
			_quoteParams.reset();
			_quoteParams._quote = _userQuote.c_str();
			_quoteParams._quoter = L"Anonymous #999";
			_quoteParams._shouldBeTrolling = false;
			_quoteParams._lang = cmdLineParams->_langType;
			if (cmdLineParams->_ghostTypingSpeed == 1)
				_quoteParams._speed = QuoteParams::slow;
			else if (cmdLineParams->_ghostTypingSpeed == 2)
				_quoteParams._speed = QuoteParams::rapid;
			else if (cmdLineParams->_ghostTypingSpeed == 3)
				_quoteParams._speed = QuoteParams::speedOfLight;

			_notepad_plus_plus_core.showQuote(&_quoteParams);
		}
		else if (cmdLineParams->_quoteType == 2) // content from file
		{
			if (doesFileExist(cmdLineParams->_easterEggName.c_str()))
			{
				bool bLoadingFailed = false;
				std::string content = getFileContent(cmdLineParams->_easterEggName.c_str(), &bLoadingFailed);
				if (!bLoadingFailed)
				{
					WcharMbcsConvertor& wmc = WcharMbcsConvertor::getInstance();
					_userQuote = wmc.char2wchar(content.c_str(), SC_CP_UTF8);
					if (!_userQuote.empty())
					{
						_quoteParams.reset();
						_quoteParams._quote = _userQuote.c_str();
						_quoteParams._quoter = L"Anonymous #999";
						_quoteParams._shouldBeTrolling = false;
						_quoteParams._lang = cmdLineParams->_langType;
						if (cmdLineParams->_ghostTypingSpeed == 1)
							_quoteParams._speed = QuoteParams::slow;
						else if (cmdLineParams->_ghostTypingSpeed == 2)
							_quoteParams._speed = QuoteParams::rapid;
						else if (cmdLineParams->_ghostTypingSpeed == 3)
							_quoteParams._speed = QuoteParams::speedOfLight;

						_notepad_plus_plus_core.showQuote(&_quoteParams);
					}
				}
			}
		}
	}

	if (cmdLineParams->_showLoadingTime)
	{
		std::chrono::steady_clock::duration nppInitTime = (std::chrono::steady_clock::now() - g_nppStartTimePoint) - g_pluginsLoadingTime - sessionLoadingTime - cmdlineParamsLoadingTime;
		std::wstringstream wss;
		wss << L"Notepad++ initialization: " << std::chrono::hh_mm_ss{ std::chrono::duration_cast<std::chrono::milliseconds>(nppInitTime) } << std::endl;
		wss << L"Plugins loading: " << std::chrono::hh_mm_ss{ std::chrono::duration_cast<std::chrono::milliseconds>(g_pluginsLoadingTime) } << std::endl;
		wss << L"Last session loading: " << std::chrono::hh_mm_ss{ std::chrono::duration_cast<std::chrono::milliseconds>(sessionLoadingTime) } << std::endl;
		wss << L"Command line params handling: " << std::chrono::hh_mm_ss{ std::chrono::duration_cast<std::chrono::milliseconds>(cmdlineParamsLoadingTime) } << std::endl;
		wss << L"Total loading time: " << std::chrono::hh_mm_ss{ std::chrono::duration_cast<std::chrono::milliseconds>(nppInitTime + g_pluginsLoadingTime + sessionLoadingTime + cmdlineParamsLoadingTime) };
		::MessageBoxW(NULL, wss.str().c_str(), L"Notepad++ loading time (hh:mm:ss.ms)", MB_OK);
	}

	if (cmdLineParams->_displayCmdLineArgs)
	{
		_notepad_plus_plus_core.command(IDM_CMDLINEARGUMENTS);
	}

	bool isSnapshotMode = nppGUI.isSnapshotMode();
	if (isSnapshotMode)
	{
		_notepad_plus_plus_core.checkModifiedDocument(false);
		// Launch backup task
		_notepad_plus_plus_core.launchDocumentBackupTask();
	}

	// Make this call later to take effect
	::SendMessage(_hSelf, NPPM_INTERNAL_SETWORDCHARS, 0, 0);
	::SendMessage(_hSelf, NPPM_INTERNAL_SETNPC, 0, 0);

	if (nppParams.doFunctionListExport())
		::SendMessage(_hSelf, NPPM_INTERNAL_EXPORTFUNCLISTANDQUIT, 0, 0);

	if (nppParams.doPrintAndExit())
		::SendMessage(_hSelf, NPPM_INTERNAL_PRNTANDQUIT, 0, 0);

	// RE2: first-run onboarding — show welcome once, gated by a marker file in the user config dir
	{
		std::wstring markerPath = nppParams.getUserPath();
		pathAppend(markerPath, L"re2_onboarded.marker");
		if (!doesFileExist(markerPath.c_str()))
		{
			showStartCenterRE2();
			HANDLE h = ::CreateFileW(markerPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h != INVALID_HANDLE_VALUE)
				::CloseHandle(h);
		}
	}
}


bool Notepad_plus_Window::isDlgsMsg(MSG *msg) const
{
	if (_notepad_plus_plus_core.processTabSwitchAccel(msg))
		return true;

	if (_notepad_plus_plus_core.processIncrFindAccel(msg))
		return true;

	if (_notepad_plus_plus_core.processFindAccel(msg))
		return true;

	for (size_t i = 0, len = _notepad_plus_plus_core._hModelessDlgs.size(); i < len; ++i)
	{
		if (::IsDialogMessageW(_notepad_plus_plus_core._hModelessDlgs[i], msg))
			return true;
	}
	return false;
}
