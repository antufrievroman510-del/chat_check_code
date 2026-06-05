@echo off
echo ========================================
echo   PWNZ AI - Mouse Sender Builder (PC1)
echo ========================================
echo.

REM Проверка наличия компилятора MSVC
where cl >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Компилятор MSVC (cl.exe) не найден!
    echo Запустите эту команду из "Developer Command Prompt for Visual Studio"
    pause
    exit /b 1
)

echo [INFO] Компиляция MouseSender.cpp...
cl /EHsc /O2 MouseSender.cpp Ws2_32.lib /Fe:MouseSender.exe

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] Сборка завершена успешно!
    echo [INFO] Файл MouseSender.exe создан.
    echo.
    echo Далее:
    echo 1. Отредактируйте config.txt и укажите IP второго ПК (читового)
    echo 2. Запустите MouseSender.exe от имени администратора
    echo 3. На втором ПК убедитесь, что чит запущен и порт 5556 открыт
    echo.
) else (
    echo.
    echo [ERROR] Ошибка компиляции!
    echo.
)

pause
