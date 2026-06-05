#pragma once
// Mock Windows header for syntax checking on Linux
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

typedef int DWORD;
typedef unsigned short WORD;
typedef long LONG;
typedef int BOOL;
typedef void* HANDLE;
typedef void* HWND;
typedef unsigned int UINT;
typedef const char* LPCSTR;

struct POINT { LONG x; LONG y; };

#define MOUSEEVENTF_MOVE 0x0001
#define MOUSEEVENTF_LEFTDOWN 0x0002
#define MOUSEEVENTF_LEFTUP 0x0004
#define MOUSEEVENTF_RIGHTDOWN 0x0008
#define MOUSEEVENTF_RIGHTUP 0x0010
#define MOUSEEVENTF_MIDDLEDOWN 0x0020
#define MOUSEEVENTF_MIDDLEUP 0x0040

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04

struct MOUSEINPUT { LONG dx; LONG dy; DWORD dwFlags; DWORD time; void* dwExtraInfo; };
struct KEYBDINPUT { WORD wVk; WORD wScan; DWORD dwFlags; DWORD time; void* dwExtraInfo; };
struct HARDWAREINPUT { DWORD uMsg; WORD wParamL; WORD wParamH; };

union INPUT_UNION { MOUSEINPUT mi; KEYBDINPUT ki; HARDWAREINPUT hi; };

struct INPUT { DWORD type; INPUT_UNION; };
#define INPUT_MOUSE 0

int SendInput(unsigned int cInputs, void* pInputs, int cbSize);
