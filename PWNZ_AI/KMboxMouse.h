#pragma once
#include "IMouseInput.h"
#include "KMBoxNet.h"
#include <string>

namespace pwnz_ai {

/**
 * @brief Реализация интерфейса IMouseInput для устройства KMBox (через UDP/Network).
 * Обертка над классом KMBoxNet.
 */
class KMboxMouse : public IMouseInput {
public:
    /**
     * @brief Конструктор.
     * @param ip IP-адрес устройства KMBox.
     * @param port Порт устройства KMBox.
     */
    explicit KMboxMouse(const std::string& ip = "192.168.1.100", int port = 8888);

    ~KMboxMouse() override;

    // Реализация интерфейса IMouseInput
    bool Init(const std::string& port = "") override;
    void Move(int dx, int dy) override;
    void Click(MouseButton button) override;
    void Press(MouseButton button) override;  // Нажатие кнопки (удержание)
    void Release(MouseButton button) override;  // Отпускание кнопки
    void Shutdown() override;
    bool IsConnected() const override;

    // Сеттеры для обновления настроек без пересоздания
    void SetConnectionInfo(const std::string& ip, int port);

private:
    std::unique_ptr<KMBoxNet> m_kmbox;
    std::string m_ip;
    int m_port;
    bool m_initialized;
};

} // namespace pwnz_ai
