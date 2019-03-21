#include "dinput8.h"

//Global Variables
int bWidth = 1024;
int bHeight = 768;

HMODULE baseModule;

HWND fixedWindowHandle = NULL;
DWORD fixCreateWindowHandleReturn;

void __declspec(naked) fixWindowHandle()
{
	__asm
	{
		push ebp
	}
	fixedWindowHandle = CreateWindowEx(0x0, "Editor", "Bloodlines", 0x0, 0, 0, bWidth, bHeight, NULL, NULL, NULL, NULL);
	__asm
	{
		pop ebp
		mov eax, fixedWindowHandle
		mov [ebp+0x24],eax
		jmp[fixCreateWindowHandleReturn]
	}
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
		strcat_s(path, "\\dinput8.ini");
		CIniReader configReader(path);

		//Load info from ini
		bWidth = configReader.ReadInteger("MAIN", "Width", 0);
		bHeight = configReader.ReadInteger("MAIN", "Height", 0);
		if (bWidth == 0 || bHeight == 0)
		{
			bWidth = 1024;
			bHeight = 768;
		}
		bool supressDisplaySettingsChange = configReader.ReadInteger("MAIN", "SuppressChangeDisplaySettings", 0) == 1;

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

		if (supressDisplaySettingsChange)
		{
			memset((void*)((DWORD)baseModule + 0x17116), 0x90, 0x1E);
		}

		float aspectRatio = bWidth * 1.0f / bHeight;
		*(float*)((DWORD)baseModule + 0xF78C) = aspectRatio;
		*(float*)((DWORD)baseModule + 0x11F76) = aspectRatio;

		//Window Create Fix
		Hook((DWORD)baseModule + 0x09015, fixWindowHandle, &fixCreateWindowHandleReturn, 0xA);

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