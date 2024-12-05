#include <windows.h>
#include <tchar.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>

// Глобальные переменные
HHOOK g_hHook = NULL;
HINSTANCE g_hDll = NULL;
HWND g_targetWindow = NULL;
DWORD g_childProcessID = 0;

std::ofstream logFile("log.txt", std::ios::app);

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
	DWORD processID;
	GetWindowThreadProcessId(hwnd, &processID);

	TCHAR windowTitle[256];
	GetWindowText(hwnd, windowTitle, sizeof(windowTitle) / sizeof(TCHAR));

	std::wcout << L"Window: " << windowTitle << L", Process ID: " << processID << " Initial: " << g_childProcessID << " Current: " << GetCurrentProcessId() << std::endl;
	if (processID == g_childProcessID) {
		g_targetWindow = hwnd;
		return FALSE; // Останавливаем поиск
	}
	return TRUE;
}

// Функция для получения имени процесса по его ID
std::wstring GetProcessName(DWORD processID) {
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);
	if (!hProcess) return L"Unknown";

	wchar_t processName[MAX_PATH] = L"<Unknown>";
	HMODULE hMod;
	DWORD cbNeeded;

	if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
		GetModuleBaseName(hProcess, hMod, processName, sizeof(processName) / sizeof(wchar_t));
	}

	CloseHandle(hProcess);
	return processName;
}

// Функция для построения цепочки процессов
void PrintProcessChain(DWORD processID) {
	std::vector<std::wstring> processChain;
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (hSnapshot == INVALID_HANDLE_VALUE) {
		std::wcerr << L"Failed to take process snapshot." << std::endl;
		return;
	}

	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof(PROCESSENTRY32);

	// Перебираем процессы, пока не найдем целевой процесс
	while (true) {
		if (Process32First(hSnapshot, &pe32)) {
			bool found = false;

			do {
				if (pe32.th32ProcessID == processID) {
					// Добавляем текущий процесс в цепочку
					std::wstring processInfo = L"Process: " + GetProcessName(pe32.th32ProcessID) +
						L" (PID: " + std::to_wstring(pe32.th32ProcessID) +
						L", Parent PID: " + std::to_wstring(pe32.th32ParentProcessID) + L")";
					processChain.push_back(processInfo);

					// Если процесс имеет родителя, продолжаем цепочку
					if (pe32.th32ParentProcessID != 0 && pe32.th32ParentProcessID != pe32.th32ProcessID) {
						processID = pe32.th32ParentProcessID;
						found = true;
						break;
					}
					else {
						found = false;
						break;
					}
				}
			} while (Process32Next(hSnapshot, &pe32));

			if (!found) break;
		}
		else {
			break;
		}
	}

	CloseHandle(hSnapshot);

	// Печатаем цепочку
	for (auto it = processChain.rbegin(); it != processChain.rend(); ++it) {
		std::wcout << *it << std::endl;
	}
}


HWND FindWindowByProcess(DWORD targetPID) {
	HWND hwnd = GetTopWindow(NULL);
	while (hwnd) {
		DWORD processID;
		GetWindowThreadProcessId(hwnd, &processID);
		//PrintProcessChain(processID);
		//std::cout << "Process ID: " << processID << " Initial: " << g_childProcessID << " Current: " << GetCurrentProcessId() << std::endl;
		if (processID == targetPID) {
			TCHAR windowTitle[256];
			GetWindowText(hwnd, windowTitle, sizeof(windowTitle) / sizeof(TCHAR));
			std::wcout << L"Found Window: " << windowTitle << std::endl;
			return hwnd;
		}
		hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
	}
	return NULL;
}


int _tmain(int argc, TCHAR* argv[]) {
	if (argc < 2) {
		std::cerr << "Usage: <executable_path>" << std::endl;
		return 1;
	}
	g_hDll = LoadLibrary(L"Hook.dll");
	if (!g_hDll) {
		std::cerr << "Failed to load DLL." << std::endl;
		return 1;
	}

	HOOKPROC hookProc = (HOOKPROC)GetProcAddress(g_hDll, "HookProc");
	if (!hookProc) {
		std::cerr << "Failed to find HookProc in DLL." << std::endl;
		FreeLibrary(g_hDll);
		return 1;
	}

	STARTUPINFO si = { sizeof(STARTUPINFO) };
	PROCESS_INFORMATION pi = {};
	if (!CreateProcess(
		argv[1],        // Путь к исполняемому файлу
		NULL,           // Аргументы
		NULL,           // Защита процесса
		NULL,           // Защита потока
		FALSE,          // Наследование дескрипторов
		0,              // Флаги создания
		NULL,           // Среда
		NULL,           // Текущая директория
		&si,            // Информация о старте
		&pi)) {         // Информация о процессе
		std::cerr << "Failed to create process." << argv[1] << std::endl;
		return 1;
	}

	g_childProcessID = pi.dwProcessId;
	DWORD winPID = 0;
	std::cout << g_childProcessID;
	Sleep(3000);
	g_targetWindow = FindWindowByProcess(g_childProcessID);
	if (!g_targetWindow) {
		HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		PROCESSENTRY32 pe32 = { sizeof(pe32) };
		if (Process32First(hSnapshot, &pe32)) {
			do {
				if (pe32.th32ParentProcessID == pi.dwProcessId) {
					std::wcout << L"Found child process: " << pe32.szExeFile
						<< L" (PID: " << pe32.th32ProcessID << L")" << std::endl;


					g_targetWindow = FindWindowByProcess(pe32.th32ProcessID);
					HWND childHwnd = NULL;
					EnumChildWindows(g_targetWindow, [](HWND child, LPARAM lParam) -> BOOL {
						DWORD childPID;
						GetWindowThreadProcessId(child, &childPID);
						std::cout << childPID << std::endl;
						if (childPID == g_childProcessID) {
							*(HWND*)lParam = child;
							return FALSE; // Найдено
						}
						return TRUE; // Продолжаем поиск
						}, (LPARAM)&childHwnd);
					if (childHwnd) g_targetWindow = childHwnd;
					winPID = pe32.th32ProcessID;
				}
			} while (Process32Next(hSnapshot, &pe32));
		}
		CloseHandle(hSnapshot);
	}

	//g_targetWindow = FindWindowByProcess(g_childProcessID);
	if (!g_targetWindow) {
		std::cerr << "Failed to find target window." << std::endl;
		TerminateProcess(pi.hProcess, 0);
		return 1;
	}

	g_hHook = SetWindowsHookExA(
		WH_KEYBOARD,       // Тип хука
		hookProc,             // Функция-хук
		g_hDll,
		GetWindowThreadProcessId(g_targetWindow, NULL));

	if (!g_hHook) {
		std::cerr << "Failed to set hook." << std::endl;
		FreeLibrary(g_hDll);
		TerminateProcess(pi.hProcess, 0);
		return 1;
	}
	WaitForSingleObject(g_targetWindow, INFINITE);
	WaitForSingleObject(pi.hProcess, INFINITE);
	UnhookWindowsHookEx(g_hHook);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	FreeLibrary(g_hDll);
	logFile.close();

	return 0;
}
