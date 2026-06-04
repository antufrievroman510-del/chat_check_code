#!/bin/bash
echo "=== Проверка синтаксиса aimbot.cpp ==="
g++ -c -std=c++17 -I. aimbot.cpp -o /tmp/aimbot_test.o 2>&1 | head -50
echo ""
echo "=== Проверка синтаксиса main.cpp ==="
g++ -c -std=c++17 -I. main.cpp -o /tmp/main_test.o 2>&1 | head -50
echo ""
echo "=== Проверка синтаксиса detector.cpp ==="
g++ -c -std=c++17 -I. detector.cpp -o /tmp/detector_test.o 2>&1 | head -50
