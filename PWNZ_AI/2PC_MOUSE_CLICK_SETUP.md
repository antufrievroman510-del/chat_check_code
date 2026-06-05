# Настройка передачи кликов мыши между двумя ПК (2PC)

## Обзор архитектуры

Система состоит из двух компьютеров:
- **ПК1 (Игровой)**: Физическая мышь подключена к этому ПК. Перехватывает нажатия хуком и отправляет их по сети.
- **ПК2 (Чит)**: Запускает чит с GUI. Получает клики по сети и эмулирует их через SendInput.

**Важно**: Движение курсора обрабатывается отдельно через Makcu (COM-порт). Этот модуль передает ТОЛЬКО нажатия кнопок.

## Поддерживаемые события

- ЛКМ (Левая кнопка) - нажатие/отпускание
- ПКМ (Правая кнопка) - нажатие/отпускание
- СКМ (Колесо нажатие) - нажатие/отпускание
- Колесо прокрутки - вверх/вниз
- X1 (Боковая кнопка "Назад") - нажатие/отпускание
- X2 (Боковая кнопка "Вперед") - нажатие/отпускание

---

## Шаг 1: Настройка ПК2 (Чит)

### 1.1 Запуск сервера приема кликов

В GUI чита перейдите во вкладку **Hardware** → **Mouse Click 2PC Settings**:

1. Включите опцию **"Enable Mouse Click UDP"** (mouse_click_udp_enabled)
2. Укажите IP адрес ПК2 (обычно это локальный IP, например `192.168.1.100`)
3. Порт по умолчанию: `5556`

### 1.2 Проверка работы сервера

Сервер автоматически запускается при включении опции. В консоли должно появиться:
```
[MouseClickServer] Started successfully on port 5556
[MouseClickServer] Listening on port 5556
```

---

## Шаг 2: Настройка ПК1 (Игровой)

### 2.1 Компиляция отправителя

Файл: `MouseClickSender.cpp`

```bash
cl /EHsc MouseClickSender.cpp Ws2_32.lib /Fe:MouseClickSender.exe
```

Или используйте готовый `.exe` файл из репозитория.

### 2.2 Запуск отправителя

1. Запустите `MouseClickSender.exe` на ПК1
2. Введите IP адрес ПК2 (где запущен чит)
3. Введите порт (по умолчанию 5556)

Пример:
```
===========================================
   Mouse Click Sender for 2PC Setup
   (C) PWNZ_AI
===========================================

Enter IP address of second PC (default: 192.168.1.100): 192.168.1.100
Enter port (default: 5556): 5556

Initializing WinSock...
[OK] Connected to 192.168.1.100:5556
Installing mouse hook...

=== LISTENING FOR MOUSE CLICKS ===
Press Ctrl+C to exit

[SEND] LMB PRESSED
[SEND] LMB RELEASED
```

---

## Шаг 3: Проверка соединения

### Тест ЛКМ:
1. Нажмите левую кнопку мыши на ПК1
2. На ПК1 в консоли должно появиться: `[SEND] LMB PRESSED`
3. На ПК2 в консоли чита должно сработать событие (проверьте логи)

### Тест ПКМ:
1. Нажмите правую кнопку мыши на ПК1
2. Проверьте логи на обоих ПК

### Тест колеса:
1. Прокрутите колесо вверх/вниз
2. Должны появиться сообщения `[SEND] WHEEL UP/DOWN`

### Тест боковых кнопок:
1. Нажмите боковые кнопки X1/X2
2. Должны появиться сообщения `[SEND] X1/X2 PRESSED/RELEASED`

---

## Шаг 4: Интеграция с читом (для разработчиков)

### Использование MouseClickNetwork в коде

```cpp
#include "MouseClickNetwork.h"

// Глобальный экземпляр
pwnz_ai::MouseClickNetwork click_sender;

// Инициализация (при запуске чита)
void init_mouse_network() {
    if (overlay.mouse_click_udp_enabled) {
        click_sender.connect(overlay.mouse_click_ip_buf, overlay.mouse_click_port);
    }
}

// Отправка событий из хука мыши
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && overlay.mouse_click_udp_enabled) {
        if (wParam == WM_LBUTTONDOWN) {
            click_sender.send_lmb_down();
        }
        else if (wParam == WM_LBUTTONUP) {
            click_sender.send_lmb_up();
        }
        else if (wParam == WM_RBUTTONDOWN) {
            click_sender.send_rmb_down();
        }
        else if (wParam == WM_RBUTTONUP) {
            click_sender.send_rmb_up();
        }
        // ... остальные кнопки
    }
    return CallNextHookEx(...);
}
```

### Обработка на стороне ПК2 (MouseClickServer)

```cpp
#include "MouseClickServer.h"

pwnz_ai::MouseClickServer click_server;

// Инициализация
void init_click_server() {
    click_server.set_lmb_callback([](bool pressed) {
        if (pressed) {
            // Эмуляция нажатия ЛКМ через SendInput
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            SendInput(1, &input, sizeof(input));
        } else {
            // Эмуляция отпускания ЛКМ
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &input, sizeof(input));
        }
    });
    
    click_server.set_rmb_callback([](bool pressed) {
        // Аналогично для ПКМ
    });
    
    click_server.start(5556);
}
```

---

## Решение проблем

### Проблема: "[ERROR] Cannot connect to ..."

**Причины:**
1. Неправильный IP адрес
2. Брандмауэр блокирует порт 5556
3. Сервер не запущен на ПК2

**Решение:**
1. Проверьте IP адрес командой `ipconfig` на ПК2
2. Добавьте правило в брандмауэр для порта 5556 (UDP)
3. Убедитесь, что опция "Enable Mouse Click UDP" включена в GUI

### Проблема: Клики не эмулируются на ПК2

**Причины:**
1. Коллбэки не установлены
2. SendInput блокируется игрой/античитом

**Решение:**
1. Проверьте установку коллбэков в коде
2. Используйте аппаратный метод (Makcu) если SendInput блокируется

### Проблема: Большая задержка

**Причины:**
1. WiFi вместо LAN кабеля
2. Высокая загрузка сети

**Решение:**
1. Используйте LAN кабель для соединения ПК1↔ПК2
2. Закройте лишние сетевые приложения

---

## Безопасность

- UDP пакеты не шифруются (локальная сеть)
- Античит не видит соединение т.к. оно идет от отдельного процесса
- Рекомендуется использовать статические IP адреса

---

## Формат пакета

```
Размер: 8 байт
Структура:
[0]     uint8_t  event_type   - Тип события (0x01-0x0C)
[1]     uint8_t  reserved     - Резерв
[2-3]   int16_t  wheel_delta  - Дельта колеса (для прокрутки)
[4-7]   int32_t  extra        - Дополнительные данные
```

Типы событий:
- `0x01` - LMB Down
- `0x02` - LMB Up
- `0x03` - RMB Down
- `0x04` - RMB Up
- `0x05` - MMB Down
- `0x06` - MMB Up
- `0x07` - Wheel Up
- `0x08` - Wheel Down
- `0x09` - X1 Down
- `0x0A` - X1 Up
- `0x0B` - X2 Down
- `0x0C` - X2 Up

---

## Контакты

При возникновении проблем обращайтесь в поддержку PWNZ_AI.
