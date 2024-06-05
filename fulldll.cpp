#include <windows.h>
#include "detours.h"
#include <iostream>
#include <tlhelp32.h> 
#pragma comment(lib, "detours.lib") 

VOID __declspec(dllexport)myfix()
{

}

static decltype(&ChangeDisplaySettingsExA) TrueChangeDisplaySettingsExA = ChangeDisplaySettingsExA;

DEVMODEA g_OriginalDevMode;
bool g_DevModeSaved = false;
HANDLE hMonitorThread = NULL;
volatile bool bMonitorThreadRunning = false;


BOOL IsProcessRunning(DWORD pid)
{
    HANDLE hProcessSnap;
    BOOL bRet = FALSE;

    PROCESSENTRY32 pe32 = { 0 };
    pe32.dwSize = sizeof(PROCESSENTRY32);

    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) return FALSE;

    if (Process32First(hProcessSnap, &pe32))
    {
        do
        {
            if (pe32.th32ProcessID == pid)
            {
                bRet = TRUE;
                break;
            }
        } while (Process32Next(hProcessSnap, &pe32));
    }

    CloseHandle(hProcessSnap);
    return bRet;
}

LONG WINAPI MyChangeDisplaySettingsExA(
    LPCSTR lpszDeviceName,
    DEVMODEA* lpDevMode,
    HWND hwnd,
    DWORD dwflags,
    LPVOID lParam)
{
    if (lpDevMode && !g_DevModeSaved)
    {
        ZeroMemory(&g_OriginalDevMode, sizeof(DEVMODEA));
        g_OriginalDevMode.dmSize = sizeof(DEVMODEA);
        if (EnumDisplaySettingsA(lpszDeviceName, ENUM_CURRENT_SETTINGS, &g_OriginalDevMode))
        {
            g_DevModeSaved = true;
        }
    }

    if (lpDevMode)
    {
        lpDevMode->dmPelsWidth = 800;
        lpDevMode->dmPelsHeight = 600;
        lpDevMode->dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    }

    return TrueChangeDisplaySettingsExA(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
}

BOOL IsWindowFullScreen(HWND hwnd)
{
    if (hwnd == NULL) return FALSE;

    RECT windowRect, desktopRect;
    GetWindowRect(hwnd, &windowRect);
    GetWindowRect(GetDesktopWindow(), &desktopRect);

    bool sizeMatches = (windowRect.left == desktopRect.left &&
        windowRect.top == desktopRect.top &&
        windowRect.right == desktopRect.right &&
        windowRect.bottom == desktopRect.bottom);

    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    
    bool hasBordersOrTitle = style & (WS_CAPTION | WS_THICKFRAME);
    bool hasWindowEdgeStyles = exStyle & (WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);

   
    if ((sizeMatches && !hasBordersOrTitle) && !hasWindowEdgeStyles) {
        return TRUE; 
    }
    else {
        return FALSE; 
    }
}

HWND FindGameWindow()
{
    return FindWindow(L"harutoma.wndclass", L"SkyFish");
}

DWORD WINAPI MonitorGameState(LPVOID lpParam)
{
    DWORD dwGamePID = GetCurrentProcessId();
    HWND hGameWnd = NULL;
    bool wasFullScreen = false; 

    while (bMonitorThreadRunning)
    {
        Sleep(1000); 

        hGameWnd = FindGameWindow();
        if (!IsProcessRunning(dwGamePID) || hGameWnd == NULL)
        {
            bMonitorThreadRunning = false; 
            if (g_DevModeSaved)
            {
                ChangeDisplaySettingsExA(NULL, &g_OriginalDevMode, NULL, CDS_UPDATEREGISTRY, NULL);
                std::cout << "Resolution restored as game exited or window not found." << std::endl;
                g_DevModeSaved = false;
            }
        }
        else
        {
            bool isCurrentFullScreen = IsWindowFullScreen(hGameWnd);
            if (wasFullScreen && !isCurrentFullScreen)
            {
                ChangeDisplaySettingsExA(NULL, &g_OriginalDevMode, NULL, CDS_UPDATEREGISTRY, NULL);
                std::cout << "Resolution restored due to game transitioning from fullscreen to windowed mode." << std::endl;
                g_DevModeSaved = false;
                bMonitorThreadRunning = false; 
            }
            wasFullScreen = isCurrentFullScreen; 
        }
    }

    return 0;
}


bool InstallHooks()
{
    DetourRestoreAfterWith();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourAttach(&(PVOID&)TrueChangeDisplaySettingsExA, MyChangeDisplaySettingsExA);

    if (DetourTransactionCommit() == NO_ERROR)
    {
        
        bMonitorThreadRunning = true;
        hMonitorThread = CreateThread(NULL, 0, MonitorGameState, NULL, 0, NULL);
        return true;
    }
    return false;
}

void RemoveHooks()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&)TrueChangeDisplaySettingsExA, MyChangeDisplaySettingsExA);
    DetourTransactionCommit();

   
    if (hMonitorThread != NULL)
    {
        bMonitorThreadRunning = false;
        WaitForSingleObject(hMonitorThread, INFINITE);
        CloseHandle(hMonitorThread);
    }

    
    if (g_DevModeSaved)
    {
        ChangeDisplaySettingsExA(NULL, &g_OriginalDevMode, NULL, CDS_UPDATEREGISTRY, NULL);
        std::cout << "Resolution restored on DLL detach." << std::endl;
        g_DevModeSaved = false;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        InstallHooks();
        break;
    case DLL_PROCESS_DETACH:
        RemoveHooks();
        break;
    }
    return TRUE;
}
