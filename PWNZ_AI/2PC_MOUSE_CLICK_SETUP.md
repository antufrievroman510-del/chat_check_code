# Решение для 2PC: Передача нажатий мыши

## 📋 Обзор

Это решение позволяет передавать **только нажатия кнопок мыши** с игрового ПК (ПК1) на читовый ПК (ПК2) через WiFi/интернет. Движения мыши обрабатываются отдельно через Makcu плату (COM-порт).

### Архитектура

```
┌─────────────────────┐                    ┌─────────────────────┐
│   ПК1 (Игровой)     │                    │   ПК2 (Чит)         │
│                     │                    │                     │
│  Физическая мышь    │                    │  MouseClickServer   │
│       │             │      WiFi/UDP      │       │             │
│       ├─→ Makcu     │◄─────5556─────►│       ├─→ SendInput     │
│       │   (движения)│      порт        │       │   (эмуляция)   │
│       │             │                    │       │             │
│       └─→ MouseClick│                    │       └─→ Аимбот    │
│           Sender    │                    │                     │
│           (отправка)│                    │                     │
└─────────────────────┘                    └─────────────────────┘
```

## 🚀 Быстрый старт

### Вариант 1: Отдельная программа для ПК1 (Рекомендуется)

#### Шаг 1: Компиляция программы для ПК1

На ПК1 (игровой) скомпилируйте `MouseClickSender.cpp`:

```bash
cl /EHsc MouseClickSender.cpp Ws2_32.lib /Fe:MouseClickSender.exe
```

Или используйте MinGW:
```bash
g++ MouseClickSender.cpp -o MouseClickSender.exe -lws2_32 -static
```

#### Шаг 2: Запуск на ПК1

1. Запустите `MouseClickSender.exe`
2. Введите IP адрес ПК2 (читового)
3. Программа начнет перехватывать нажатия ЛКМ/ПКМ и отправлять их на ПК2

#### Шаг 3: Интеграция в чит на ПК2

В файле `main.cpp` или там где инициализируется чит, добавьте:

```cpp
#include "MouseClickServer.h"
#include "MouseController.h"

// Глобальный сервер кликов
pwnz_ai::MouseClickServer clickServer;

// Инициализация (вызвать при старте чита)
void InitClickServer() {
    // Установка коллбэка для ЛКМ
    clickServer.set_lmb_callback([](bool pressed) {
        if (pressed) {
            // Эмуляция нажатия ЛКМ
            MouseController::GetInstance().PressButton(VK_LBUTTON);
            std::cout << "[CLICK] LMB PRESSED (from network)\n";
        } else {
            // Эмуляция отпускания ЛКМ
            MouseController::GetInstance().ReleaseButton(VK_LBUTTON);
            std::cout << "[CLICK] LMB RELEASED (from network)\n";
        }
    });

    // Установка коллбэка для ПКМ (опционально)
    clickServer.set_rmb_callback([](bool pressed) {
        if (pressed) {
            MouseController::GetInstance().PressButton(VK_RBUTTON);
        } else {
            MouseController::GetInstance().ReleaseButton(VK_RBUTTON);
        }
    });

    // Запуск сервера на порту 5556
    if (!clickServer.start(5556)) {
        std::cerr << "[ERROR] Failed to start click server!\n";
    }
}
```

#### Шаг 4: Настройка сети

1. **На ПК2 (чит):** Назначьте статический IP (например, `192.168.1.100`)
2. **Откройте порт 5556 UDP** в брандмауэре Windows:
   ```powershell
   netsh advfirewall firewall add rule name="PWNZ Click Server" dir=in action=allow protocol=UDP localport=5556
   ```
3. **Убедитесь что оба ПК в одной сети** (WiFi или роутер)

---

### Вариант 2: Библиотека в обоих процессах

Если вы хотите больше контроля, можно использовать классы напрямую:

#### На ПК1 (в процессе игры или отдельном процессе):

```cpp
#include "MouseClickNetwork.h"

MouseClickNetwork clickNet;

// Подключение к ПК2
if (clickNet.connect("192.168.1.100", 5556)) {
    // В цикле аимбота или хуке мыши
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
        clickNet.send_lmb(true);
    } else {
        clickNet.send_lmb(false);
    }
}
```

#### На ПК2 (в чите):

См. код выше в Шаге 3.

---

## 🔧 Детальная настройка

### Изменение порта

По умолчанию используется порт **5556**. Чтобы изменить:

```cpp
// На обоих ПК укажите одинаковый порт
clickServer.start(5557);  // ПК2
clickNet.connect(ip, 5557);  // ПК1
```

### Отладка и логи

Обе программы выводят логи в консоль:

**ПК1 (Sender):**
```
[SEND] LMB PRESSED
[SEND] LMB RELEASED
[OK] Connected to 192.168.1.100:5556
```

**ПК2 (Server):**
```
[MouseClickServer] Started successfully on port 5556
[MouseClickServer] Received click from 192.168.1.50
```

### Автопереподключение

`MouseClickSender.exe` автоматически пытается переподключиться каждые 2 секунды если соединение потеряно.

---

## 🛡️ Безопасность (Античит)

### Почему это безопасно?

1. **Сетевое соединение идет от чита**, а не от процесса игры
2. **MouseClickSender** - отдельный процесс, античит игры его не видит
3. **Makcu плата** определяется как обычное USB устройство (клавиатура/мышь)
4. **Нет инъекций DLL** в процесс игры

### Рекомендации:

✅ Используйте отдельную программу `MouseClickSender.exe` для ПК1  
✅ Не внедряйте сетевой код в процесс игры  
✅ Используйте стандартный UDP порт (можно сменить с 5556 на другой)  
✅ Отключайте логирование в релизной версии  

---

## 📁 Структура файлов

```
PWNZ_AI/
├── MouseClickSender.cpp       # Программа для ПК1 (отправка кликов)
├── MouseClickNetwork.h        # Библиотека клиента (альтернатива для ПК1)
├── MouseClickServer.h         # Библиотека сервера (для ПК2, вшить в чит)
└── 2PC_MOUSE_CLICK_SETUP.md   # Эта документация
```

---

## ❓ Частые проблемы

### Проблема: "Cannot connect to ..."
**Решение:**
1. Проверьте что ПК2 имеет статический IP
2. Убедитесь что порт 5556 открыт в брандмауэре ПК2
3. Проверьте что оба ПК в одной сети (ping между ними)

### Проблема: Клики не эмулируются на ПК2
**Решение:**
1. Проверьте что коллбэки установлены до вызова `start()`
2. Убедитесь что `MouseController` инициализирован
3. Запустите чит от имени администратора (нужно для SendInput)

### Проблема: Большая задержка
**Решение:**
1. Используйте WiFi 5GHz вместо 2.4GHz
2. Убедитесь что нет других устройств нагружающих сеть
3. Проверьте пинг: `ping 192.168.1.100 -t`

---

## 🎯 Пример полной интеграции

### Файл: `main.cpp` (ПК2, чит)

```cpp
#include "MouseClickServer.h"
#include "MouseController.h"
#include <iostream>

// Глобальный сервер
static pwnz_ai::MouseClickServer g_clickServer;

void Initialize2PCClicks() {
    std::cout << "[INIT] Setting up 2PC click receiver...\n";

    // Коллбэк для ЛКМ (стрельба)
    g_clickServer.set_lmb_callback([](bool pressed) {
        auto& mc = MouseController::GetInstance();
        
        if (pressed) {
            mc.PressButton(VK_LBUTTON);
            // Можно добавить триггербот логику здесь
        } else {
            mc.ReleaseButton(VK_LBUTTON);
        }
    });

    // Коллбэк для ПКМ (прицеливание)
    g_clickServer.set_rmb_callback([](bool pressed) {
        auto& mc = MouseController::GetInstance();
        
        if (pressed) {
            mc.PressButton(VK_RBUTTON);
        } else {
            mc.ReleaseButton(VK_RBUTTON);
        }
    });

    // Запуск
    if (g_clickServer.start(5556)) {
        std::cout << "[INIT] Click server running on port 5556\n";
    } else {
        std::cerr << "[ERROR] Failed to start click server!\n";
    }
}

int main() {
    // ... инициализация чита ...
    
    // Инициализация 2PC кликов
    Initialize2PCClicks();
    
    // ... основной цикл чита ...
    
    return 0;
}
```

---

## 📞 Поддержка

Если возникли проблемы:
1. Проверьте логи в консоли обеих программ
2. Убедитесь что порты совпадают
3. Проверьте сетевое соединение (ping)
4. Убедитесь что брандмауэр не блокирует порт 5556

**Удачи в настройке! 🎮**
