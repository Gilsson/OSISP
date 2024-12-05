#ifndef HOOK_H
#define HOOK_H

#include <windows.h>
#include "pch.h"

#define HOOKDLL_API __declspec(dllexport)


extern "C" HOOKDLL_API LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);


#endif // HOOK_H
