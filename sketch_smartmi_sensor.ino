#include <CapacitiveSensor.h>

#define SENDER_PIN 3
#define SENSOR_PIN 2
#define LED_PIN 13 // Встроенный светодиод "L" на плате Arduino Nano

#define SAMPLES_NUMBER 300
#define TIMEOUT_MS 100

// Твои проверенные пороги воды
#define MIN_READING 1040
#define MAX_READING 2550

byte packet[] = {0xFA, 0x29, 0x03, 0x00, 0x00, 0x00, 0x00, 0x14, 0x9A, 0x00, 0x00, 0x00, 0x03, 0x77, 0x72, 0x71, 0x03, 0x00, 0x6C, 0x4C, 0x3B, 0x03, 0x2F, 0x15, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x03, 0xE8, 0x00, 0x2c, 0x02, 0x6D, 0x37, 0xD2, 0x00};

byte checksum;
byte mappedValue;
long readingRaw;
long smoothReading = MAX_READING; 

// Переменные временного фильтра для фиксации плавного высыхания
unsigned long lowWaterStartTime = 0;
bool isLowWaterTimerRunning = false;

// Флаги и переменные для управления звуковыми сигналами
bool overflowTriggered = false; 
int lowWaterBeepCount = 0;          
unsigned long lastLowWaterBeep = 0; 

CapacitiveSensor sensor = CapacitiveSensor(SENDER_PIN, SENSOR_PIN); 

void setup()                    
{
   pinMode(LED_PIN, OUTPUT);   
   digitalWrite(LED_PIN, LOW); // Изначально тушим его
   
   sensor.set_CS_AutocaL_Millis(0xFFFFFFFF); 
   sensor.set_CS_Timeout_Millis(TIMEOUT_MS);
   Serial.begin(9600); 
}

void loop()                    
{    
  checksum = 0;
  packet[11] = 0; // Очищаем 11-ю ячейку перед записью
  
  readingRaw = sensor.capacitiveSensorRaw((int)SAMPLES_NUMBER); 
  unsigned long currentMillis = millis();
  
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
    
    while (smoothReading == -1 || (smoothReading >= 0 && smoothReading < 1000)) {
      checksum = 0;
      packet[11] = 0; 
      for(int i = 0; i < 42; i++) { checksum ^= packet[i]; } 
      checksum ^= 0xA0;
      packet[42] = checksum; 
      Serial.write(packet, sizeof(packet)); 
      delay(100); 
      
      readingRaw = sensor.capacitiveSensorRaw((int)SAMPLES_NUMBER);
      if (readingRaw == -1 || readingRaw == -2) smoothReading = readingRaw;
      else smoothReading = (smoothReading * 0.85) + (readingRaw * 0.15);
    }
  }
  // === 3. ШТАТНЫЙ ДИАПАЗОН РАБОТЫ ИСПАРЕНИЯ ===
  // === SECTION 3. NORMAL EVAPORATION & AUDIO CONTROL ===
  else {
    mappedValue = constrain(map(smoothReading, MIN_READING, MAX_READING, 0, 120), 0, 120); 
    
    if (smoothReading > (MIN_READING + 300)) {
      overflowTriggered = false;
    }
    
    // Сбрасываем триггеры, только если бак залили выше половины (больше 12 единиц)
    if (mappedValue > 12) {
      lowWaterBeepCount = 0;
      isLowWaterTimerRunning = false;
    }

    // === ИСПРАВЛЕНО: ЖЕСТКИЙ ДИАПАЗОН ПРЕДУПРЕЖДЕНИЯ (ОТ 1 ДО 11 ЕДИНИЦ) ===
    // === EARLY LOW WATER ALARM WINDOW (2 TO 11 UNITS) ===
    // Ловим воду строго в коридоре от половины бака до последней капли, пока mappedValue не стал нулем
    if (mappedValue > 1 && mappedValue <= 11) {
      if (!isLowWaterTimerRunning) {
        lowWaterStartTime = currentMillis; 
        isLowWaterTimerRunning = true;
      }
      
      // Если уровень стабильно находится в этом коридоре больше 3 секунд:
      // [RU] ТАЙМЕР СТАБИЛЬНОСТИ: Если уровень держится в коридоре более 3 секунд (3000 мс)
      // [EN] VALIDATION FILTER: If the level stays continuously inside window for more than 3 seconds
      if (isLowWaterTimerRunning && (currentMillis - lowWaterStartTime >= 3000UL)) {
        
        if ((lowWaterBeepCount == 0) || (lowWaterBeepCount < 3 && currentMillis - lastLowWaterBeep >= 8000UL)) { 
          
          lowWaterBeepCount++;            
          lastLowWaterBeep = currentMillis; 
          
          digitalWrite(LED_PIN, HIGH); // Железно зажигаем лампу L
          Serial.flush();
          delay(10);
          
          unsigned long startLowWater = millis();
          while (millis() - startLowWater < 1500) { 
            checksum = 0;
            packet[11] = 125; 
            for(int i = 0; i < 42; i++) { checksum ^= packet[i]; } 
            checksum ^= 0xA0;
            packet[42] = checksum; 
            Serial.write(packet, sizeof(packet)); 
            delay(60); 
          }
          digitalWrite(LED_PIN, LOW); // Тушим лампу L
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
