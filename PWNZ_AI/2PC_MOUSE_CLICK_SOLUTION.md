# Решение для передачи нажатий мыши между ПК (2PC Setup)

## Проблема
Нужно передавать нажатия кнопок мыши с игрового ПК на читовый ПК, при этом:
- Движения мыши уже передаются через Makcu плату (COM-порт)
- Не использовать LAN кабель (только WiFi/интернет)
- Античит не должен детектировать передачу

## Решение: MouseClickNetwork + MouseClickServer

### Архитектура

```
┌─────────────────┐                    ┌─────────────────┐
│   ПК1 (Игровой) │                    │   ПК2 (Чит)     │
│                 │                    │                 │
│  Физ. мышь ─────┤                    │  Чит запускает  │
│  Makcu плата    │   USB (COM-порт)   │  MouseController│
│                 │   ───────────────► │  с MakcuInput   │
│  Кнопки мыши    │                    │                 │
│      │          │                    │  ▲              │
│      ▼          │                    │  │              │
│  MouseClick     │   UDP 5556         │  │              │
│  Network        │   ───────────────► │  │              │
│  (клиент)       │   (WiFi/интернет)  │  │              │
│                 │                    │  │              │
│                 │                    │  │              │
│                 │                    │  MouseClick     │
│                 │                    │  Server         │
│                 │                    │  (сервер)       │
│                 │                    │                 │
└─────────────────┘                    └─────────────────┘
```

### Как это работает

1. **Движения мыши**: Передаются напрямую через Makcu плату по COM-порту
   - ПК1: Физическая мышь подключена к Makcu
   - ПК2: `MakcuInput` класс читает движения через COM-порт

2. **Нажатия кнопок**: Передаются по сети через UDP
   - ПК1: `MouseClickNetwork` отправляет пакеты с состоянием кнопок
   - ПК2: `MouseClickServer` получает пакеты и эмулирует нажатия через SendInput

### Почему античит не спалит?

1. **Сетевое соединение идет от чита**, а не от игры
   - Античит видит исходящее соединение от процесса чита
   - Это выглядит как обычное сетевое взаимодействие (обновление, телеметрия)

2. **UDP вместо TCP**
   - Меньше задержка
   - Нет постоянного соединения (отправляем только при клике)

3. **Раздельные порты**
   - Порт 5555: aim_data (координаты для аимбота)
   - Порт 5556: click_data (нажатия кнопок)

4. **Эмуляция на стороне чита**
   - SendInput вызывается на ПК2 где чит
   - Игра на ПК1 видит только физические нажатия от Makcu

## Использование

### На ПК1 (Игровой) - Отправка кликов

```cpp
#include "MouseClickNetwork.h"

// Инициализация
MouseClickNetwork clickNet;
clickNet.connect("192.168.1.100", 5556); // IP читового ПК

// В цикле аимбота - отправка нажатий
if (should_shoot) {
    clickNet.send_lmb(true);  // Нажать LMB
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    clickNet.send_lmb(false); // Отпустить LMB
}

if (is_aiming) {
    clickNet.send_rmb(true);  // Нажать RMB (прицеливание)
} else {
    clickNet.send_rmb(false); // Отпустить RMB
}
```

### На ПК2 (Чит) - Прием кликов

```cpp
#include "MouseClickServer.h"
#include "MouseController.h"

// Инициализация сервера
MouseClickServer clickServer;
clickServer.set_lmb_callback([](bool pressed) {
    if (pressed) {
        MouseController::GetInstance().PressButton(VK_LBUTTON);
    } else {
        MouseController::GetInstance().ReleaseButton(VK_LBUTTON);
    }
});

clickServer.set_rmb_callback([](bool pressed) {
    if (pressed) {
        MouseController::GetInstance().PressButton(VK_RBUTTON);
    } else {
        MouseController::GetInstance().ReleaseButton(VK_RBUTTON);
    }
});

// Запуск сервера
clickServer.start(5556);

// В main loop чита ничего менять не нужно - коллбэки работают асинхронно
```

## Интеграция с существующим кодом

### В overlay.cpp (ПК1 - отправка)

```cpp
// Добавить в начало файла
#include "MouseClickNetwork.h"

// Глобальный экземпляр (или в классе Aimbot)
static MouseClickNetwork g_clickNetwork;

// В функции инициализации
void InitClickNetwork() {
    g_clickNetwork.connect("192.168.1.100", 5556);
}

// В аимботе где определяется стрельба
if (target_acquired && should_fire) {
    // Отправка клика на читовый ПК
    g_clickNetwork.send_lmb(true);
    
    // Эмуляция короткого нажатия
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    
    g_clickNetwork.send_lmb(false);
}
```

### В main.cpp (ПК2 - прием)

```cpp
// Добавить в начало файла
#include "MouseClickServer.h"

// Глобальный сервер
static MouseClickServer g_clickServer;

// В main() после инициализации
g_clickServer.set_lmb_callback([](bool pressed) {
    if (pressed) {
        MouseController::GetInstance().PressButton(VK_LBUTTON);
    } else {
        MouseController::GetInstance().ReleaseButton(VK_LBUTTON);
    }
});

g_clickServer.set_rmb_callback([](bool pressed) {
    if (pressed) {
        MouseController::GetInstance().PressButton(VK_RBUTTON);
    } else {
        MouseController::GetInstance().ReleaseButton(VK_RBUTTON);
    }
});

g_clickServer.start(5556);
```

## Настройка сети

### Статический IP (рекомендуется)
На читовом ПК настроить статический IP:
```
IP: 192.168.1.100
Маска: 255.255.255.0
Шлюз: 192.168.1.1
```

### Брандмауэр Windows
Разрешить входящие подключения на порт 5556:
```powershell
netsh advfirewall firewall add rule name="PWNZ AI Clicks" dir=in action=allow protocol=UDP localport=5556
```

## Преимущества этого решения

1. ✅ **Безопасность**: Античит не видит подозрительной активности
2. ✅ **Без LAN кабеля**: Работает через WiFi/интернет
3. ✅ **Минимальная задержка**: UDP + отдельный поток
4. ✅ **Разделение ответственности**: Движения через Makcu, клики через сеть
5. ✅ **Простая интеграция**: Минимальные изменения в существующем коде

## Альтернативные варианты

### Вариант 2: Bluetooth HID
- Требует Bluetooth адаптер на обоих ПК
- Может быть замедленнее чем UDP
- Более сложная настройка

### Вариант 3: Виртуальный COM-порт через сеть
- Эмуляция COM-порта поверх TCP
- Большая задержка чем прямой UDP
- Сложнее в реализации

### Вариант 4: Shared Memory + Network Bridge
- Требует драйвер ядра
- Максимальная производительность
- Высокий риск детекта античитом

## Рекомендации

1. Используйте статический IP для читового ПК
2. Настройте QoS на роутере для приоритета UDP трафика
3. Тестируйте задержку в вашей сети (должна быть < 5мс в локальной сети)
4. Добавьте логирование для отладки проблем с подключением
5. Рассмотрите возможность шифрования пакетов если используете интернет (не локальную сеть)
