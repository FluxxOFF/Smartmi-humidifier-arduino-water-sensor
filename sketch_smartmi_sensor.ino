#include <CapacitiveSensor.h>

// --- АППАРАТНАЯ НАСТРОЙКА ПИНОВ / HARDWARE PIN CONFIGURATION ---
#define SENDER_PIN 3 // [RU] Пин импульса (через 1М) / [EN] Pulse pin (via 1M resistor)
#define SENSOR_PIN 2 // [RU] Пин замера пластины в баке / [EN] Sensor pin connected to the tank plate
#define LED_PIN 13   // [RU] Встроенный светодиод L для аварий / [EN] Built-in L LED for alerts indication

// --- НАСТРОЙКИ ЧУВСТВИТЕЛЬНОСТИ / SENSITIVITY & SPEED SETTINGS ---
#define SAMPLES_NUMBER 300 // [RU] Количество микро-замеров / [EN] Number of micro-samples per reading
#define TIMEOUT_MS 100     // [RU] Тайм-аут ожидания импульса / [EN] Sensor response timeout in milliseconds

// --- КАЛИБРОВОЧНЫЕ ПОРОГИ / CALIBRATION THRESHOLDS ---
#define MIN_READING 1040   // [RU] Полный бак с водой / [EN] Full tank threshold (minimum capacitance)
#define MAX_READING 2550   // [RU] Абсолютно пустой бак / [EN] Empty tank threshold (maximum capacitance)

// --- СТРУКТУРНЫЙ ПАКЕТ XIAOMI / XIAOMI SERIAL PACKET ARRAY ---
// [RU] packet[11] = Уровень/Звук, packet[42] = Контрольная сумма XOR
// [EN] packet[11] = Level/Audio code, packet[42] = Final XOR Checksum
byte packet[] = {0xFA, 0x29, 0x03, 0x00, 0x00, 0x00, 0x00, 0x14, 0x9A, 0x00, 0x00, 0x00, 0x03, 0x77, 0x72, 0x71, 0x03, 0x00, 0x6C, 0x4C, 0x3B, 0x03, 0x2F, 0x15, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x03, 0xE8, 0x00, 0x2c, 0x02, 0x6D, 0x37, 0xD2, 0x00};

// --- СЛУЖЕБНЫЕ ПЕРЕМЕННЫЕ / SYSTEM VARIABLES ---
byte checksum;                     // [RU] Расчет контрольной суммы / [EN] Variable for XOR checksum calculation
byte mappedValue;                  // [RU] Уровень воды для Xiaomi (0-120) / [EN] Scaled water level value (0-120)
long readingRaw;                   // [RU] Сырой замер датчика / [EN] Raw sensor capacitance value
long smoothReading = MAX_READING;  // [RU] Отфильтрованное значение / [EN] Filtered capacitance value (EMA)

// --- ТАЙМЕРЫ ЗВУКА И ИСПАРЕНИЯ / TIMERS & AUDIO FLAGS ---
unsigned long lowWaterStartTime = 0; // [RU] Таймер стабильности коридора / [EN] Stability timer for the low water window
bool isLowWaterTimerRunning = false; // [RU] Флаг запущенного таймера / [EN] Flag indicating if the low water timer is running

bool overflowTriggered = false;      // [RU] Предохранитель перелива / [EN] Overflow alert latch to prevent sound loop
int lowWaterBeepCount = 0;           // [RU] Счетчик писков (макс 3) / [EN] Low water beep loop counter (max 3 times)
unsigned long lastLowWaterBeep = 0;  // [RU] Таймер 8с интервала писков / [EN] Timer for the 8-second interval between beeps

// [RU] Инициализация библиотеки датчика / [EN] Initialize CapacitiveSensor object
CapacitiveSensor sensor = CapacitiveSensor(SENDER_PIN, SENSOR_PIN); 

void setup()                    
{
   pinMode(LED_PIN, OUTPUT);   // [RU] Настройка пина диода / [EN] Configure built-in LED pin as output
   digitalWrite(LED_PIN, LOW); // [RU] Тушим диод при старте / [EN] Turn off the LED on system boot
   
   sensor.set_CS_AutocaL_Millis(0xFFFFFFFF); // [RU] Отключение автокалибровки / [EN] Disable library autocalibration
   sensor.set_CS_Timeout_Millis(TIMEOUT_MS); // [RU] Передача тайм-аута 100 мс / [EN] Pass custom timeout to the library
   Serial.begin(9600);                       // [RU] Запуск UART на 9600 бод / [EN] Initialize hardware UART at 9600 baud
}

void loop()                    
{    
  checksum = 0;    // [RU] Очистка чексуммы / [EN] Reset checksum before calculation
  packet[11] = 0; // [RU] Очистка ячейки данных / [EN] Clear data index 11 before writing new level
  
  // [RU] Чтение емкости. (int) убирает варнинги / [EN] Read capacitance. (int) cast prevents type warnings
  readingRaw = sensor.capacitiveSensorRaw((int)SAMPLES_NUMBER); 
  unsigned long currentMillis = millis(); // [RU] Фиксация времени / [EN] Get current processor uptime in ms
  
  // === МАТЕМАТИЧЕСКИЙ ФИЛЬТР СГЛАЖИВАНИЯ / MATHEMATICAL SMOOTHING FILTER (EMA) ===
  if (readingRaw != -1 && readingRaw != -2) {
    // [RU] Формула фильтра: 85% истории и 15% нового замера. Убирает шум вентилятора.
    // [EN] EMA Filter formula: 85% of history and 15% of new sample. Dampens fan noise.
    smoothReading = (smoothReading * 0.85) + (readingRaw * 0.15); 
  } else {
    smoothReading = readingRaw; // [RU] Ошибки пропускаем мгновенно / [EN] Pass critical errors (-1, -2) instantly
  }
  
  // =====================================================================================
  // === БЛОК 1. ОБРАБОТКА ПЕРЕЛИВА ВОДЫ / SECTION 1. OVERFLOW HANDLING (KЗ / SHORT CIRCUIT) ===
  // =====================================================================================
  if (smoothReading == -2) {
    lowWaterBeepCount = 0;          // [RU] Сброс счетчика писков дна / [EN] Reset low water beep counter on refill
    isLowWaterTimerRunning = false; // [RU] Стоп секундомера нехватки / [EN] Stop the low water validation timer
    
    if (!overflowTriggered) {
      overflowTriggered = true;     // [RU] Блок повторных сигналов / [EN] Lock further overflow signals till level drops
      
      digitalWrite(LED_PIN, HIGH);  // [RU] Включаем светодиод L / [EN] Turn on the built-in L LED
      Serial.flush();               // [RU] Очистка буфера передачи / [EN] Flush TX serial buffer before alarm
      delay(10);
      
      // [RU] Отправка кода 125 в течение 1 секунды / [EN] Fire code 125 loop strictly for 1 second (1000 ms)
      unsigned long startOverflow = millis();
      while (millis() - startOverflow < 1000) { 
        checksum = 0;
        packet[11] = 125; // [RU] Аварийный код 125 / [EN] Write overflow alert code 125 into index 11
        
        for(int i = 0; i < 42; i++) { checksum ^= packet[i]; } // [RU] Расчет XOR / [EN] Calculate XOR checksum
        checksum ^= 0xA0;        // [RU] Финальная маска Xiaomi / [EN] Apply final Xiaomi packet mask
        packet[42] = checksum;   // [RU] Запись чексуммы в конец / [EN] Pack checksum into index 42
        
        Serial.write(packet, sizeof(packet)); // [RU] Отправка в Xiaomi / [EN] Send packet array to Xiaomi CPU
        delay(60);                            // [RU] Шаг очереди / [EN] Inter-packet delay step
      }
      digitalWrite(LED_PIN, LOW);   // [RU] Тушим светодиод L / [EN] Turn off L LED after alarm finishes
    }
    mappedValue = 120; // [RU] Держим 5 полосок на экране / [EN] Force full 5 bars display on humidifier panel
  }
  
  // =====================================================================================
  // === БЛОК 2. КРЫШКУ СНЯЛИ В ВОЗДУХ / SECTION 2. LID LIFTED (SILENCE MODE & RESET) ===
  // =====================================================================================
  else if (smoothReading == -1 || (smoothReading >= 0 && smoothReading < 1000)) {
    mappedValue = 0;                // [RU] Жесткий ноль полосок в воздухе / [EN] Force 0 bars in air, screen dims
    overflowTriggered = false;      // [RU] Разблокировка триггера перелива / [EN] Unlatch overflow flag since lid is off
    lowWaterBeepCount = 0;          // [RU] Запрет звука при съеме руками / [EN] Reset low water counters, silence forced
    isLowWaterTimerRunning = false; // [RU] Сброс таймера высыхания / [EN] Terminate the dry tank validation timer
    digitalWrite(LED_PIN, LOW);     // [RU] Выключение светодиода L / [EN] Ensure built-in L LED is off
    
    // [RU] БЛОКИРУЮЩИЙ ЦИКЛ ВОЗДУХА: Удерживает тишину и ноль, пока голова снята
    // [EN] BLOCKING AIR LOOP: Maintains absolute silence and zero bars while the lid is off
    while (smoothReading == -1 || (smoothReading >= 0 && smoothReading < 1000)) {
      checksum = 0;
      packet[11] = 0; // [RU] Шлем стабильный 0 полосок / [EN] Keep sending stable 0 bars level
      
      for(int i = 0; i < 42; i++) { checksum ^= packet[i]; } 
      checksum ^= 0xA0;
      packet[42] = checksum; 
      
      Serial.write(packet, sizeof(packet)); 
      delay(100); // [RU] Шаг отправки пакетов воздуха / [EN] Send packet every 100 ms
      
      // [RU] Постоянный опрос датчика внутри цикла воздуха / [EN] Continuous sensor polling inside air loop
      readingRaw = sensor.capacitiveSensorRaw((int)SAMPLES_NUMBER);
      if (readingRaw == -1 || readingRaw == -2) smoothReading = readingRaw;
      else smoothReading = (smoothReading * 0.85) + (readingRaw * 0.15);
    }
  }
  
  // =====================================================================================
  // === БЛОК 3. ШТАТНОЕ ИСПАРЕНИЕ И КОНТРОЛЬ ЗВУКА / SECTION 3. NORMAL EVAPORATION & AUDIO CONTROL ===
  // =====================================================================================
  else {
    // [RU] Перевод емкости 1040-2550 в полоски Xiaomi 0-120 / [EN] Map raw capacitance 1040-2550 to Xiaomi 0-120 scale
    mappedValue = constrain(map(smoothReading, MIN_READING, MAX_READING, 0, 120), 0, 120); 
    
    // [RU] Сброс флага перелива при падении уровня / [EN] Unlatch overflow flag once water level drops safely
    if (smoothReading > (MIN_READING + 300)) {
      overflowTriggered = false;
    }
    
    // [RU] Воду долили выше аварийной зоны (> 12) — сброс писков / [EN] Water refilled past alert zone (>12) — reset counters
    if (mappedValue > 12) {
      lowWaterBeepCount = 0;
      isLowWaterTimerRunning = false;
    }

    // === КОРИДОР УПРЕЖДЕНИЯ ЗВУКА ДНА (ОТ 2 ДО 11 ЕДИНИЦ) / EARLY LOW WATER ALARM WINDOW (1 TO 11 UNITS) ===
    if (mappedValue > 1 && mappedValue <= 11) {
      // [RU] Если зашли в коридор впервые — запускаем секундомер / [EN] If entered window for the first time — start timer
      if (!isLowWaterTimerRunning) {
        lowWaterStartTime = currentMillis; 
        isLowWaterTimerRunning = true;
      }
      
      // [RU] ТАЙМЕР СТАБИЛЬНОСТИ: Если уровень держится в коридоре более 3 секунд (3000 мс)
      // [EN] VALIDATION FILTER: If the level stays continuously inside window for more than 3 seconds
