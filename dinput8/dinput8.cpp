#include "dinput8.h"
#include <detours.h>

//Global Variables
int bWidth = 1024;
int bHeight = 768;

HMODULE baseModule;
char CurrentGameDir[MAX_PATH];

static HWND (__stdcall *TrueWindowFromPoint)(POINT Point) = WindowFromPoint;
HWND DetourWindowFromPoint(POINT Point)
{
	return CreateWindowEx(0x0, "Editor", "Bloodline", WS_BORDER, 0, 0, bWidth, bHeight, NULL, NULL, NULL, NULL);
}

static LONG(__stdcall* TrueChangeDisplaySettings)(DEVMODE* lpDevMode, DWORD dwFlags) = ChangeDisplaySettings;
LONG DetourChangeDisplaySettings(DEVMODE* lpDevMode, DWORD dwFlags)
{
	return DISP_CHANGE_SUCCESSFUL;
}

static BOOL(__stdcall* TrueCreateDirectoryA)(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes) = CreateDirectoryA;
BOOL DetourCreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
	auto result = strstr(lpPathName, CurrentGameDir);

	if (strstr(lpPathName, CurrentGameDir))
	{
		return TrueCreateDirectoryA(lpPathName, lpSecurityAttributes);
	}
	else if (strstr(lpPathName, "temp") || strstr(lpPathName, "save"))
	{
		return TrueCreateDirectoryA(lpPathName, lpSecurityAttributes);
	}
	return TRUE;
}


//Dll Main
bool WINAPI DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		//Get module location and its ini file
		char path[MAX_PATH];
		HMODULE hm = NULL;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)"dinput8.dll", &hm);
		GetModuleFileNameA(hm, path, sizeof(path));
		*strrchr(path, '\\') = '\0';
		strcpy_s(CurrentGameDir, MAX_PATH, path);
		strcat_s(CurrentGameDir, MAX_PATH, "\\");
		strcat_s(path, "\\dinput8.ini");
		CIniReader configReader(path);

		//Load info from ini
		bWidth = configReader.ReadInteger("MAIN", "Width", 640);
		bHeight = configReader.ReadInteger("MAIN", "Height", 480);
		if (bWidth < 0 || bHeight < 0)
		{
			bWidth = 1024;
			bHeight = 768;
		}

		std::string loadAdditionalDLLName = configReader.ReadString("MAIN", "LoadDll", "");
		
		//Get dll from Windows directory
		GetSystemDirectory(path, MAX_PATH);
		strcat_s(path, "\\dinput8.dll");

		//Set pointers
		dinput8.dll = LoadLibraryA(path);
		dinput8.DirectInput8Create = (LPWDirectInput8Create)GetProcAddress(dinput8.dll, "DirectInput8Create");
		dinput8.DllCanUnloadNow = (LPWDllCanUnloadNow)GetProcAddress(dinput8.dll, "DllCanUnloadNow");
		dinput8.DllGetClassObject = (LPWDllGetClassObject)GetProcAddress(dinput8.dll, "DllGetClassObject");
		dinput8.DllRegisterServer = (LPWDllRegisterServer)GetProcAddress(dinput8.dll, "DllRegisterServer");
		dinput8.DllUnregisterServer = (LPWDllUnregisterServer)GetProcAddress(dinput8.dll, "DllUnregisterServer");

		//Get base module
		baseModule = GetModuleHandle(NULL);
		UnprotectModule(baseModule);

		float aspectRatio = bWidth * 1.0f / bHeight;
		*(float*)((DWORD)baseModule + 0xF78C) = aspectRatio;
		*(float*)((DWORD)baseModule + 0x11F76) = aspectRatio;

		//Window Create Fix
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)TrueWindowFromPoint, DetourWindowFromPoint);
		if (configReader.ReadBoolean("MAIN", "RestrictFolderCreation", true))
			DetourAttach(&(PVOID&)TrueCreateDirectoryA, DetourCreateDirectoryA);
		if (configReader.ReadBoolean("MAIN", "SuppressChangeDisplaySettings", false))
			DetourAttach(&(PVOID&)TrueChangeDisplaySettings, DetourChangeDisplaySettings);
		DetourTransactionCommit();

		//LoadLibary
		if (SuiString_EndsWith(loadAdditionalDLLName, ".dll"))
		{
			LoadLibraryA(loadAdditionalDLLName.c_str());
		}

		break;
	}
	case DLL_PROCESS_DETACH:
	{
		FreeLibrary(hModule);
		break;
	}
	return true;
	}

	return TRUE;
}


HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID * ppvOut, LPUNKNOWN punkOuter)
{
	HRESULT hr = dinput8.DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);

	return hr;
}

HRESULT WINAPI DllCanUnloadNow()
{
	return dinput8.DllCanUnloadNow();
}

HRESULT WINAPI DllGetClassObject(REFCLSID riidlsid, REFIID riidltf, LPVOID whatever)
{
	return dinput8.DllGetClassObject(riidlsid, riidltf, whatever);
}

HRESULT WINAPI DllRegisterServer()
{
	return dinput8.DllRegisterServer();
}

HRESULT WINAPI DllUnregisterServer()
{
	return dinput8.DllUnregisterServer();
}