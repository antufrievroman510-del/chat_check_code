#pragma once

// РљСЂРёС‚РёС‡РµСЃРєРё РІР°Р¶РЅРѕ: РѕРїСЂРµРґРµР»СЏРµРј РјР°РєСЂРѕСЃС‹ Р”Рћ Р»СЋР±С‹С… Р·Р°РіРѕР»РѕРІРєРѕРІ Windows
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// Генератор полиморфного мусора, который компилятор не сможет вырезать
#define JUNK_CODE_1 \
    volatile int junk1_##__LINE__ = __COUNTER__ * 3; \
    volatile int junk2_##__LINE__ = __LINE__ + 15; \
    if (junk1_##__LINE__ == junk2_##__LINE__) { junk1_##__LINE__ += 5; }

#define JUNK_CODE_2 \
    volatile float f_junk_##__LINE__ = 3.14f * __LINE__; \
    volatile int i_junk_##__LINE__ = __COUNTER__; \
    for (volatile int i = 0; i < (i_junk_##__LINE__ % 2); ++i) { f_junk_##__LINE__ += 1.0f; }

#define JUNK_CODE_3 \
    volatile DWORD dw_junk_##__LINE__ = GetTickCount(); \
    dw_junk_##__LINE__ ^= __LINE__;

// Главный макрос мутации сигнатуры
#define MUTATE_SIGNATURE \
    JUNK_CODE_1; \
    JUNK_CODE_2; \
    JUNK_CODE_3;