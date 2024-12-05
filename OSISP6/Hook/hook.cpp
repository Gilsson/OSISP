#include "pch.h"
#include "hook.h"
#include <fstream>
#include <windows.h>
#include <mutex>
#include <string>
#include <string>
#include <iostream>
#include <codecvt>
#include <locale>
std::mutex logMutex;
std::wofstream logFile("hook_log.txt", std::ios::app | std::ios::binary);


std::string MessageToString(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_PAINT: return "WM_PAINT";
	case WM_KEYDOWN: return "WM_KEYDOWN";
	case WM_KEYUP: return "WM_KEYUP";
	case WM_MOUSEMOVE: return "WM_MOUSEMOVE";
	case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
	case WM_LBUTTONUP: return "WM_LBUTTONUP";
	case WM_GETICON: return "WM_GETICON";
	case WM_NCCREATE: return "WM_NCCREATE";
	case WM_SETCURSOR: {
		HWND hWnd = (HWND)wParam; // Окно, содержащее курсор
		int hitTestResult = LOWORD(lParam); // Результат проверки "хит-теста"
		int mouseMessage = HIWORD(lParam); // Сообщение окна
		return "WM_SETCURSOR: hWnd=" + std::to_string((uintptr_t)hWnd) +
			", hitTestResult=" + std::to_string(hitTestResult) +
			", mouseMessage=" + std::to_string(mouseMessage);
	};
	case WM_NCHITTEST: {
		int x = LOWORD(lParam); // Координата X
		int y = HIWORD(lParam); // Координата Y
		return "WM_NCHITTEST: x=" + std::to_string(x) + ", y=" + std::to_string(y);
	};
					 // Добавьте другие сообщения по мере необходимости
	default: return "Unknown message (" + std::to_string(msg) + ")";
	}
}

std::wstring TranslateKeyInfo(LPARAM lParam, WPARAM wParam) {
	std::wstring result;

	// Извлекаем repeat count (0-15 биты)
	int repeatCount = lParam & 0xFFFF;
	result += L"Repeat Count: " + std::to_wstring(repeatCount) + L"\n";

	// Извлекаем scan code (16-23 биты)
	int scanCode = (lParam >> 16) & 0xFF;

	int vk = MapVirtualKeyExW(scanCode, MAPVK_VSC_TO_VK_EX, 0);
	wchar_t unicodeChar[2] = { 0 };
	BYTE kb[256];
	GetKeyboardState(kb);
	wchar_t charCode;
	result += L"Scan Code: ";
	result += std::to_wstring(scanCode);
	result += L'\n';
	int charsWritten = ToUnicode(vk, scanCode, kb, unicodeChar, 2, 0);

	// Обработка результата ToUnicode
	if (charsWritten > 0) {
		result += L"Unicode Character: ";
		result += unicodeChar;
		result += L"\n";
	}
	else {
		result += L"Unicode Character: [No valid character]\n";
	}
	// Извлекаем extended key flag (24 бит)
	bool isExtendedKey = (lParam & 0x01000000) != 0;
	result += L"Extended Key: " + std::wstring(isExtendedKey ? L"Yes" : L"No") + L"\n";

	// Извлекаем context code (29 бит)
	bool isAltDown = (lParam & 0x20000000) != 0;
	result += L"ALT Down: " + std::wstring(isAltDown ? L"Yes" : L"No") + L"\n";

	// Извлекаем previous key state (30 бит)
	bool isKeyDownBefore = (lParam & 0x40000000) != 0;
	result += L"Previous Key State: " + std::wstring(isKeyDownBefore ? L"Down" : L"Up") + L"\n";

	// Извлекаем transition state (31 бит)
	bool isKeyPress = (lParam & 0x80000000) == 0;
	result += L"Transition State: " + std::wstring(isKeyPress ? L"Pressed" : L"Released") + L"\n";

	// Извлекаем keycode (WPARAM)
	result += L"Keycode: " + std::to_wstring(wParam) + L"\n";
	result += L"\n";
	return result;
}

LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
	/*if (nCode >= 0 && nCode == HC_ACTION) {
		CWPSTRUCT* msgInfo = (CWPSTRUCT*)lParam;
		std::lock_guard<std::mutex> lock(logMutex);
		std::string messageStr = MessageToString(msgInfo->message, wParam, lParam);
		logFile << "Message: " << messageStr
			<< " (Code: " << msgInfo->message << ")"
			<< ", WPARAM: " << msgInfo->wParam
			<< ", LPARAM: " << msgInfo->lParam
			<< std::endl;
	}*/
	//if (nCode >= 0) { // Если сообщение подлежит обработке
	//	MOUSEHOOKSTRUCT* msgInfo = (MOUSEHOOKSTRUCT*)lParam;

	//	logFile << "Mouse Event:" << std::endl;
	//	logFile << "Coordinates: (" << msgInfo->pt.x << ", " << msgInfo->pt.y << ")" << std::endl;
	//	logFile << "Window Handle (HWND): " << msgInfo->hwnd << std::endl;

	//	// Интерпретация wHitTestCode
	//	logFile << "Hit Test Code: " << msgInfo->wHitTestCode;
	//	switch (msgInfo->wHitTestCode) {
	//	case HTCLIENT:
	//		logFile << " (Client Area)" << std::endl;
	//		break;
	//	case HTCAPTION:
	//		logFile << " (Window Title Bar)" << std::endl;
	//		break;
	//	case HTNOWHERE:
	//		logFile << " (Outside Window)" << std::endl;
	//		break;
	//	default:
	//		logFile << " (Other)" << std::endl;
	//		break;
	//	}

	//	logFile << "Extra Info: " << msgInfo->dwExtraInfo << std::endl;
	//}
	if (nCode == HC_ACTION) {

		std::wstring result = TranslateKeyInfo(lParam, wParam);
		logFile << result;

		/*if (wParam == WM_KEYDOWN) {
			KBDLLHOOKSTRUCT* kbdStruct = (KBDLLHOOKSTRUCT*)lParam;
			DWORD wVirtKey = kbdStruct->vkCode;
			DWORD wScanCode = kbdStruct->scanCode;
			logFile << wVirtKey << ' ' << wScanCode << std::endl;
		}*/
	}

	//}
	//}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}
