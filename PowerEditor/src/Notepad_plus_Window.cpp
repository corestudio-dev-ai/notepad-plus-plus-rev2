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


// RE2 Start Center: TaskDialog-based launcher with "blank / blank HTML / web project" command links.
void Notepad_plus_Window::showStartCenterRE2()
{
	TASKDIALOG_BUTTON buttons[] = {
		{ 1001, L"Blank document\nStart with an empty file." },
		{ 1002, L"Blank HTML file\nNew file pre-filled with HTML5 boilerplate." },
		{ 1003, L"New web project\nCreates index.html, style.css, script.js in a folder you pick." },
	};

	TASKDIALOGCONFIG tdc = {};
	tdc.cbSize = sizeof(tdc);
	tdc.hwndParent = _hSelf;
	tdc.hInstance = _hInst;
	tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
	tdc.pszWindowTitle = L"Notepad++ RE2";
	tdc.pszMainIcon = TD_INFORMATION_ICON;
	tdc.pszMainInstruction = L"Welcome. What would you like to start with?";
	tdc.pszContent = L"Pick a starting point, or Cancel to keep the empty document.";
	tdc.cButtons = _countof(buttons);
	tdc.pButtons = buttons;
	tdc.nDefaultButton = 1001;

	int selected = 0;
	if (SUCCEEDED(TaskDialogIndirect(&tdc, &selected, nullptr, nullptr)))
	{
		switch (selected)
		{
			case 1002: createBlankHtmlRE2(); break;
			case 1003: createWebProjectRE2(); break;
			default: break;
		}
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
