# Кастомный датчик уровня воды для Xiaomi Smartmi Humidifier 2 (Arduino Nano)
# Custom Water Level Sensor for Xiaomi Smartmi Humidifier 2 (Arduino Nano)

[RU] Реверс-инжиниринг и замена штатной платы датчиков уровня воды для моек воздуха Xiaomi Smartmi Humidifier 2 (модель SKV6001RT) с использованием Arduino Nano.
[EN] Reverse engineering and software replacement for the stock water level sensor board of the Xiaomi Smartmi Humidifier 2 (model SKV6001RT) using Arduino Nano.

## 📌 Особенности / Features
* **[RU] Упреждение звука:** Сигнал аварии (код 125) срабатывает синхронно с миганием первой полоски на увлажнителе, не дожидаясь блокировки UART-порта со стороны Xiaomi.
* **[EN] Early Audio Alert:** The alarm (code 125) triggers synchronously with the flashing of the first water bar, preventing the Xiaomi CPU from locking the UART port.
* **[RU] Фильтр сглаживания (EMA):** Пропорция 85% истории / 15% нового замера полностью убирает наводки от вентилятора и мотора.
* **[EN] Smoothing Filter (EMA):** A blend of 85% history / 15% new reading completely eliminates electromagnetic noise from the fan and motor.
* **[RU] Режим тишины:** При съеме крышки руками Arduino мгновенно замолкает, гасит экран и удерживает тишину.
* **[EN] Silence Mode:** When the lid is lifted by hand, Arduino immediately silences the alerts, turns off the display, and maintains quiet.

## 🔌 Распиновка / Pinout
* `D3` -> [1 MOm Resistor] -> `D2` -> Металлическая пластина в баке / Metal plate in the tank.
* `TX (D1)` -> `RX` Увлажнителя / Humidifier RX.
* `GND` -> `GND` Увлажнителя / Humidifier GND.
* `5V` -> `+5V` Питание увлажнителя / Humidifier +5V power.

## 🛠️ Калибровка / Calibration
* `#define MIN_READING 1040` — Полный бак / Full tank.
* `#define MAX_READING 2550` — Пустой бак / Empty tank.
