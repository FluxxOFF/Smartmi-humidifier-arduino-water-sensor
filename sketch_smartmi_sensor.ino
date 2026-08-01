#include <CapacitiveSensor.h>

// --- АППАРАТНАЯ НАСТРОЙКА ПИНОВ / HARDWARE PIN CONFIGURATION ---
#define SENDER_PIN 3 // [RU] Пин импульса (через 1М) / [EN] Pulse pin (via 1M resistor)
#define SENSOR_PIN 2 // [RU] Пин замера пластины в баке / [EN] Sensor pin connected to the tank plate
#define LED_PIN 13   // [RU] Встроенный светодиод L на плате / [EN] Built-in L LED on Arduino board

// --- НАСТРОЙКИ ЧУВСТВИТЕЛЬНОСТИ / SENSITIVITY & SPEED SETTINGS ---
#define SAMPLES_NUMBER 300 // [RU] Количество микро-замеров / [EN] Number of micro-samples per reading
#define TIMEOUT_MS 100     // [RU] Тайм-аут ожидания импульса / [EN] Sensor response timeout in milliseconds

// --- КАЛИБРОВОЧНЫЕ ПОРОГИ / CALIBRATION THRESHOLDS ---
#define MIN_READING 1040 // [RU] Полный бак с водой / [EN] Full tank threshold (minimum capacitance)
#define MAX_READING 2550 // [RU] Абсолютно пустой бак / [EN] Empty tank threshold (maximum capacitance)

// --- СТРУКТУРНЫЙ ПАКЕТ XIAOMI / XIAOMI SERIAL PACKET ARRAY ---
// [RU] packet[11] = Уровень/Звук, packet[42] = Контрольная сумма XOR
// [EN] packet[11] = Level/Audio code, packet[42] = Final XOR Checksum
byte packet[] = {0xFA, 0x29, 0x03, 0x00, 0x00, 0x00, 0x00, 0x14, 0x9A, 0x00, 0x00, 0x00, 0x03, 0x77, 0x72, 0x71, 0x03, 0x00, 0x6C, 0x4C, 0x3B, 0x03, 0x2F, 0x15, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x03, 0xE8, 0x00, 0x2c, 0x02, 0x6D, 0x37, 0xD2, 0x00};

// --- СЛУЖЕБНЫЕ ПЕРЕМЕННЫЕ / SYSTEM VARIABLES ---
byte checksum;                     // [RU] Расчет контрольной суммы / [EN] Variable for XOR checksum calculation
byte mappedValue;                  // [RU] Уровень воды для Xiaomi (0-120) / [EN] Scaled water level value (0-120)
long readingRaw;                   // [RU] Сырой замер датчика / [EN] Raw sensor capacitance value
long smoothReading = MAX_READING;  // [RU] Отфильтрованное значение (EMA) / [EN] Filtered capacitance value (EMA)

// --- ТАЙМЕРЫ ЗВУКА И ИСПАРЕНИЯ / TIMERS & AUDIO FLAGS ---
// [RU] Переменные временного фильтра для фиксации плавного высыхания
// [EN] Timers for validating steady low water condition
unsigned long lowWaterStartTime = 0; // [RU] Таймер стабильности коридора / [EN] Uptime milestone for low water window
bool isLowWaterTimerRunning = false; // [RU] Флаг запущенного таймера / [EN] Flag indicating if the low water timer is running

// [RU] Флаги и переменные для управления звуковыми сигналами
// [EN] Flags and variables for audio alert management
bool overflowTriggered = false;      // [RU] Предохранитель перелива / [EN] Overflow alert latch to prevent sound loop
int lowWaterBeepCount = 0;           // [RU] Счетчик писков (макс 3) / [EN] Low water beep loop counter (max 3 times) 
unsigned long lastLowWaterBeep = 0;  // [RU] Таймер 8с интервала писков / [EN] Timestamp of the last low water alarm burst

// [RU] Инициализация библиотеки датчика / [EN] Initialize CapacitiveSensor object
CapacitiveSensor sensor = CapacitiveSensor(SENDER_PIN, SENSOR_PIN); 

void setup()                    
{
   pinMode(LED_PIN, OUTPUT);    // [RU] Настройка пина диода / [EN] Configure built-in LED pin as output 
   digitalWrite(LED_PIN, LOW);  // [RU] Изначально тушим его / [EN] Turn off the LED on system boot
   
   sensor.set_CS_AutocaL_Millis(0xFFFFFFFF); // [RU] Отключение автокалибровки / [EN] Disable library autocalibration
   sensor.set_CS_Timeout_Millis(TIMEOUT_MS); // [RU] Передача тайм-аута 100 мс / [EN] Pass custom timeout to the library
   Serial.begin(9600); // [RU] Запуск UART на 9600 бод / [EN] Initialize hardware UART at 9600 baud
}

void loop()                    
{    
  checksum = 0; // [RU] Очистка чексуммы / [EN] Reset checksum before calculation
  packet[11] = 0; // [RU] Очищаем 11-ю ячейку перед записью / [EN] Clear data index 11 before writing level
  
  readingRaw = sensor.capacitiveSensorRaw((int)SAMPLES_NUMBER); 
  unsigned long currentMillis = millis(); // [RU] Фиксация времени / [EN] Get current processor uptime in ms
  
  // === МАТЕМАТИЧЕСКИЙ ФИЛЬТР СГЛАЖИВАНИЯ НАВОДОК ===
  // === MATHEMATICAL SMOOTHING FILTER (EMA) ===
  if (readingRaw != -1 && readingRaw != -2) {
    // [RU] Формула фильтра: 85% истории и 15% нового замера. Убирает шум вентилятора.
    // [EN] EMA Filter formula: 85% of history and 15% of new sample. Dampens fan noise.
    smoothReading = (smoothReading * 0.85) + (readingRaw * 0.15); 
  } else {
    smoothReading = readingRaw; 
  }
  
  // === 1. МОМЕНТ ПЕРЕЛИВА ИЛИ МАКСИМУМА ===
  // === SECTION 1. OVERFLOW HANDLING (SHORT CIRCUIT) ===
  if (smoothReading == -2) {
    lowWaterBeepCount = 0; 
    isLowWaterTimerRunning = false;
    if (!overflowTriggered) {
      overflowTriggered = true; 
      
      digitalWrite(LED_PIN, HIGH); 
      Serial.flush();
      delay(10);
      
      unsigned long startOverflow = millis();
      while (millis() - startOverflow < 1000) { 
        checksum = 0;
        packet[11] = 125; 
        for(int i = 0; i < 42; i++) { checksum ^= packet[i]; } 
        checksum ^= 0xA0;
        packet[42] = checksum; 
        Serial.write(packet, sizeof(packet)); 
        delay(60); 
      }
      digitalWrite(LED_PIN, LOW); 
    }
    mappedValue = 120; 
  }
  // === 2. КРЫШКУ СНЯЛИ В ВОЗДУХ ===
  // === SECTION 2. LID LIFTED (SILENCE MODE & RESET) ===
  else if (smoothReading == -1 || (smoothReading >= 0 && smoothReading < 1000)) {
    mappedValue = 0;           
    overflowTriggered = false; 
    lowWaterBeepCount = 0;     
    isLowWaterTimerRunning = false; 
    digitalWrite(LED_PIN, LOW);   
     
    // [RU] БЛОКИРУЮЩИЙ ЦИКЛ: Удерживает тишину и ноль, пока голова снята
    // [EN] BLOCKING LOOP: Maintains absolute silence and zero bars while the lid is off
    while (smoothReading == -1 || (smoothReading >= 0 && smoothReading < 1000)) {
      checksum = 0;  
      packet[11] = 0; // [RU] Шлем стабильный 0 полосок / [EN] Keep sending stable 0 bars level
      for(int i = 0; i < 42; i++) { checksum ^= packet[i]; } 
      checksum ^= 0xA0;
      packet[42] = checksum; 
      Serial.write(packet, sizeof(packet)); 
      delay(100); 
       
     // [RU] Постоянный опрос датчика внутри цикла / [EN] Continuous sensor polling inside loop
      readingRaw = sensor.capacitiveSensorRaw((int)SAMPLES_NUMBER);
      if (readingRaw == -1 || readingRaw == -2) smoothReading = readingRaw;
      else smoothReading = (smoothReading * 0.85) + (readingRaw * 0.15);
    }
  }
  // === 3. ШТАТНЫЙ ДИАПАЗОН РАБОТЫ ИСПАРЕНИЯ ===
  // === SECTION 3. NORMAL EVAPORATION & AUDIO CONTROL ===
  else {
      // [RU] Перевод емкости 1040-2550 в полоски Xiaomi 0-120 / [EN] Map raw capacitance 1040-2550 to Xiaomi 0-120 scale
    mappedValue = constrain(map(smoothReading, MIN_READING, MAX_READING, 0, 120), 0, 120); 

     // [RU] Сброс флага перелива при падении уровня / [EN] Unlatch overflow flag once water level drops safely
    if (smoothReading > (MIN_READING + 300)) {
      overflowTriggered = false;
    }
    
    // [RU] Сбрасываем триггеры, только если бак залили выше половины (больше 12 единиц)
    // [EN] Reset timers only if the tank is refilled above 12 units
    if (mappedValue > 12) {
      lowWaterBeepCount = 0;
      isLowWaterTimerRunning = false;
    }

    // === ИСПРАВЛЕНО: ЖЕСТКИЙ ДИАПАЗОН ПРЕДУПРЕЖДЕНИЯ (ОТ 1 ДО 11 ЕДИНИЦ) ===
    // === EARLY LOW WATER ALARM WINDOW (2 TO 11 UNITS) ===
    // [RU] Ловим воду строго в коридоре от половины бака до последней капли, пока mappedValue не стал нулем
    // [EN] Capture the water level strictly inside the safe window before it drops down to absolute zero
    if (mappedValue > 1 && mappedValue <= 11) {
      if (!isLowWaterTimerRunning) {
        lowWaterStartTime = currentMillis; 
        isLowWaterTimerRunning = true;
      }
      
      // [RU] ТАЙМЕР СТАБИЛЬНОСТИ: Если уровень держится в коридоре более 3 секунд (3000 мс)
      // [EN] VALIDATION FILTER: If the level stays continuously inside window for more than 3 seconds
      if (isLowWaterTimerRunning && (currentMillis - lowWaterStartTime >= 3000UL)) {

        // [RU] Проверка лимита в 3 повторения и интервала паузы в 8 секунд (8000 мс)
        // [EN] Verify the 3 beeps limit and the 8-second interval restriction
        if ((lowWaterBeepCount == 0) || (lowWaterBeepCount < 3 && currentMillis - lastLowWaterBeep >= 8000UL)) { 
          
          lowWaterBeepCount++; // [RU] Продвигаемся к лимиту 3 раз / [EN] Increment beep counter towards max limit           
          lastLowWaterBeep = currentMillis; // [RU] Запоминаем время писка / [EN] Save current millisecond timestamp
           
          digitalWrite(LED_PIN, HIGH); // [RU] Включаем светодиод L / [EN] Turn on the built-in L LED
          Serial.flush(); // [RU] Очистка буфера передачи / [EN] Flush TX serial buffer before alarm
          delay(10);

          // [RU] Отправка кода 125 в течение 1.5 секунды / [EN] Fire code 125 loop strictly for 1.5 second (1500 ms)
          unsigned long startLowWater = millis();
          while (millis() - startLowWater < 1500) { 
            checksum = 0;
            packet[11] = 125;  // [RU] Аварийный код 125 / [EN] Write overflow alert code 125 into index 11
            for(int i = 0; i < 42; i++) { checksum ^= packet[i]; } // [RU] Расчет XOR / [EN] Calculate XOR checksum
            checksum ^= 0xA0;  // [RU] Финальная маска Xiaomi / [EN] Apply final Xiaomi packet mask
            packet[42] = checksum; // [RU] Запись чексуммы в конец / [EN] Pack checksum into index 42
            Serial.write(packet, sizeof(packet)); // [RU] Отправка в Xiaomi / [EN] Send packet array to Xiaomi CPU
           delay(60); // [RU] Шаг очереди / [EN] Inter-packet delay step
          }
          digitalWrite(LED_PIN, LOW); // [RU] Тушим светодиод L / [EN] Turn off L LED after alarm finishes
        }
      }
    } else {
      isLowWaterTimerRunning = false;
    }
  }
    
  packet[11] = mappedValue; // Записываем штатное значение уровня в 11-ю ячейку

  /* Расчет контрольной суммы */
  /* TRANSMIT STANDARD REFRESH PACKET TO UPDATE HUMIDIFIER DISPLAY BARS */
  for(int i = 0; i < 42; i++) { checksum ^= packet[i]; } 
  checksum ^= 0xA0;
  packet[42] = checksum; 
  
  Serial.write(packet, sizeof(packet)); 
  delay(100);
}
