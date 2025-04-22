#include <DHT.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>

// MQTT Properties
const char* MQTT_BROKER = "broker.emqx.io";
const char* MQTT_TOPIC_TEMP = "iot/tmp";
const char* MQTT_TOPIC_SOIL = "iot/agua";  // Nuevo tópico para humedad del suelo
const char* MQTT_TOPIC_GAS = "iot/gas";    // Nuevo tópico para el sensor MQ-135
const int MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "esp32";

// Pin Definitions
const int DHT_PIN = 13;        // DHT11 sensor pin
const int SOIL_SENSOR_PIN = 35; // Soil moisture sensor pin
const int RED_PIN = 17;        // RGB LED red pin
const int GREEN_PIN = 16;      // RGB LED green pin
const int BLUE_PIN = 15;       // RGB LED blue pin
const int RELAY_PIN = 2;       // Water pump relay pin
const int LIGHT_SENSOR_PIN = 34; // Light sensor pin
const int MQ135_PIN = 32;      // MQ-135 air quality sensor pin
const int BUZZER_PIN = 14;     // Buzzer pin

// Display settings
const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

// Threshold values
const float TEMP_MIN = 15.0;
const float TEMP_MAX = 30.0;
const int SOIL_MOISTURE_MIN = 60;
const int SOIL_MOISTURE_MAX = 70;
const int AIR_QUALITY_THRESHOLD = 1000;  // Ajustar según calibración del MQ-135

// WiFi credentials
const char* ssid = "iPhone de Noe";
const char* password = "123412345";

// Variables para almacenar valores anteriores
float lastTemperature = 0;
float lastHumidity = 0;
int lastSoilMoisture = 0;
int lastLightLevel = 0;
int lastAirQuality = 0;
bool lastIrrigationState = false;

// Umbrales para detectar cambios significativos
const float TEMP_CHANGE_THRESHOLD = 0.5;    // 0.5°C de cambio
const float HUMIDITY_CHANGE_THRESHOLD = 2;  // 2% de cambio
const int SOIL_CHANGE_THRESHOLD = 3;        // 3% de cambio
const int LIGHT_CHANGE_THRESHOLD = 5;       // 5% de cambio
const int AIR_QUALITY_CHANGE_THRESHOLD = 50; // Cambio en el valor del sensor

// Tiempos de medición y actualización (en milisegundos)
const unsigned long SENSOR_READ_INTERVAL = 5000;  // Leer sensores cada 5 segundos
unsigned long lastSensorReadTime = 0;

// Tiempos de calentamiento para sensores y actuadores
const unsigned long WARMUP_TIME = 60000;     // 60 segundos (1 minuto) de calentamiento
unsigned long startTime = 0;                 // Tiempo de inicio del sistema
bool systemWarmedUp = false;                 // Indicador de si el sistema ya pasó el tiempo de calentamiento

// Variables para manejar el sensor MQ135
int mq135BaselineValue = -1;  // Valor inicial no establecido
const int MQ135_SAMPLES = 20;  // Más muestras para mejor precisión

// Initialize objects
DHT dht(DHT_PIN, DHT11);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClient espClient;
PubSubClient client(espClient);

// Global variables
int failCount = 0;
bool valuesChanged = false;

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on topic ");
  Serial.print(topic);
  Serial.print(": ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

bool connectWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 10) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nError connecting to WiFi.");
    return false;
  }
}

bool subscribeMQTT() {
  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callback);
  
  Serial.println("Attempting to connect to MQTT broker...");
  
  // Intentar conectar con un tiempo de espera mayor
  int retries = 0;
  while (!client.connected() && retries < 5) {
    if (client.connect(MQTT_CLIENT_ID)) {
      client.subscribe(MQTT_TOPIC_TEMP);
      client.subscribe(MQTT_TOPIC_SOIL);
      client.subscribe(MQTT_TOPIC_GAS);
      Serial.println("Connected to MQTT broker and subscribed to topics:");
      Serial.println("- " + String(MQTT_TOPIC_TEMP) + " (temperatura)");
      Serial.println("- " + String(MQTT_TOPIC_SOIL) + " (humedad del suelo)");
      Serial.println("- " + String(MQTT_TOPIC_GAS) + " (calidad del aire)");
      return true;
    }
    
    Serial.print(".");
    retries++;
    delay(1000);
  }
  
  Serial.println("\nFailed to connect to MQTT broker");
  return false;
}

int readAnalogAverage(int pin, int samples = 10) {
  long sumValue = 0;
  for (int i = 0; i < samples; i++) {
    sumValue += analogRead(pin);
    delay(10);
  }
  return sumValue / samples;
}

bool isTemperatureOptimal(float temp) {
  return temp >= TEMP_MIN && temp <= TEMP_MAX;
}

bool isSoilMoistureOptimal(int moisture) {
  return moisture >= SOIL_MOISTURE_MIN && moisture <= SOIL_MOISTURE_MAX;
}

bool isAirQualityGood(int airQuality) {
  // Si el sistema está en periodo de calentamiento, consideramos el aire como bueno
  if (!systemWarmedUp || mq135BaselineValue == -1) {
    return true;
  }
  
  // Usamos el valor relativo para determinar la calidad del aire
  // Si el valor actual es más de AIR_QUALITY_THRESHOLD por encima del valor base,
  // consideramos que la calidad del aire es mala
  return (airQuality - mq135BaselineValue) < AIR_QUALITY_THRESHOLD;
}

void updateRGBLed(bool isOptimal) {
  digitalWrite(RED_PIN, !isOptimal);
  digitalWrite(GREEN_PIN, isOptimal);
  digitalWrite(BLUE_PIN, LOW);
  Serial.printf("LED State - RED: %s, GREEN: %s\n", 
                isOptimal ? "OFF" : "ON", 
                isOptimal ? "ON" : "OFF");
}

bool controlIrrigation(int soilMoisture) {
  bool irrigationState = false;
  
  // No activar el riego durante el periodo de calentamiento
  if (!systemWarmedUp) {
    digitalWrite(RELAY_PIN, LOW);
    return false;
  }
  
  if (soilMoisture < SOIL_MOISTURE_MIN) {
    digitalWrite(RELAY_PIN, HIGH);
    irrigationState = true;
  } else if (soilMoisture > SOIL_MOISTURE_MAX) {
    digitalWrite(RELAY_PIN, LOW);
    irrigationState = false;
  } else {
    // Mantener el estado actual si está en el rango óptimo
    irrigationState = digitalRead(RELAY_PIN) == HIGH;
  }
  
  return irrigationState;
}

void controlBuzzer(bool isAirPolluted) {
  // No activar el buzzer durante el periodo de calentamiento
  if (!systemWarmedUp || mq135BaselineValue == -1) {
    noTone(BUZZER_PIN);
    return;
  }
  
  if (isAirPolluted) {
    // Activar el buzzer cuando la calidad del aire es mala
    tone(BUZZER_PIN, 2000); // Frecuencia de 2000Hz
    Serial.println("¡Alerta! Calidad del aire deficiente. Buzzer activado.");
  } else {
    // Desactivar el buzzer
    noTone(BUZZER_PIN);
  }
}

void updateDisplay(float temp, float humidity, int soilMoisture, int lightLevel, int airQuality, bool isIrrigating) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Mostrar mensaje de calentamiento durante el periodo inicial
  if (!systemWarmedUp) {
    display.setCursor(0, 0);
    display.println("Calentando sensores...");
    display.setCursor(0, 10);
    unsigned long remainingTime = (WARMUP_TIME - (millis() - startTime)) / 1000;
    display.printf("Tiempo restante: %lu seg", remainingTime);
    
    // Mostrar barra de progreso
    int progress = ((millis() - startTime) * 100) / WARMUP_TIME;
    progress = constrain(progress, 0, 100);
    display.drawRect(0, 30, 128, 10, SSD1306_WHITE);
    display.fillRect(2, 32, (124 * progress) / 100, 6, SSD1306_WHITE);
    
    display.setCursor(0, 50);
    display.println("Espere para operacion completa");
    
    display.display();
    return;
  }
  
  // Temperatura
  display.setCursor(0, 0);
  display.printf("Temp: %.1fC", temp);
  if (isTemperatureOptimal(temp)) {
    display.setCursor(80, 0);
    display.print("Optima");
  } else {
    display.setCursor(80, 0);
    display.print("Alerta");
  }
  
  // Humedad ambiental
  display.setCursor(0, 10);
  display.printf("Hum: %.0f%%", humidity);
  
  // Humedad del suelo
  display.setCursor(0, 20);
  display.printf("Suelo: %d%%", soilMoisture);
  if (isSoilMoistureOptimal(soilMoisture)) {
    display.setCursor(80, 20);
    display.print("Optima");
  } else if (soilMoisture < SOIL_MOISTURE_MIN) {
    display.setCursor(80, 20);
    display.print("Seco");
  } else {
    display.setCursor(80, 20);
    display.print("Humedo");
  }
  
  // Nivel de luz
  display.setCursor(0, 30);
  display.printf("Luz: %d%%", lightLevel);
  
  // Calidad del aire
  display.setCursor(0, 40);
  // Mostrar el valor del sensor (absoluto) y la diferencia con respecto al valor base
  if (mq135BaselineValue != -1) {
    display.printf("Aire: %d (+%d)", airQuality, airQuality - mq135BaselineValue);
  } else {
    display.printf("Aire: %d (calib)", airQuality);
  }
  
  if (isAirQualityGood(airQuality)) {
    display.setCursor(80, 40);
    display.print("Normal");
  } else {
    display.setCursor(80, 40);
    display.print("Alerta");
  }
  
  // Estado del riego (mejorado y más visible)
  display.setCursor(0, 50);
  display.printf("Riego: %s", isIrrigating ? "ACTIVO" : "INACTIVO");
  
  // Destacar el estado del riego si está activo
  if (isIrrigating) {
    display.fillRect(75, 50, 53, 8, SSD1306_BLACK); // Limpia el área
    display.fillRect(75, 50, 53, 8, SSD1306_INVERSE); // Invierte para destacar
    display.setCursor(80, 50);
    display.print("BOMBEAR");
  }
  
  display.display();
}

void initializeSystem() {
  // Initialize pins
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  // Test RGB LED
  for (int pin : {RED_PIN, GREEN_PIN, BLUE_PIN}) {
    digitalWrite(pin, HIGH);
    delay(500);
    digitalWrite(pin, LOW);
  }
  
  // Test buzzer (breve sonido para verificar funcionamiento)
  tone(BUZZER_PIN, 1000);
  delay(200);
  noTone(BUZZER_PIN);
  
  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Iniciando sistema...");
  display.display();
  
  // Asegurarnos de que la bomba esté apagada
  digitalWrite(RELAY_PIN, LOW);
  
  // Resetear la bandera de calentamiento y registrar el tiempo de inicio
  systemWarmedUp = false;
  startTime = millis();
  
  // Resetear el valor de calibración del MQ135
  mq135BaselineValue = -1;
  
  Serial.println("System initialized!");
  Serial.printf("Periodo de calentamiento: %d segundos\n", WARMUP_TIME / 1000);
}

// Función para detectar si un cambio es significativo
bool isChangeSignificant(float oldValue, float newValue, float threshold) {
  return abs(newValue - oldValue) >= threshold;
}

// Calibrar el sensor MQ135 al final del periodo de calentamiento
void calibrateMQ135() {
  int sum = 0;
  int samples = MQ135_SAMPLES;
  
  Serial.println("\nCalibrando sensor MQ135...");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Calibrando MQ135...");
  display.display();
  
  // Tomar múltiples muestras para obtener un valor base más preciso
  for (int i = 0; i < samples; i++) {
    sum += analogRead(MQ135_PIN);
    
    // Actualizar la pantalla con el progreso
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Calibrando MQ135...");
    display.setCursor(0, 10);
    display.printf("Progreso: %d%%", (i * 100) / samples);
    display.setCursor(0, 20);
    display.printf("Muestra: %d de %d", i + 1, samples);
    display.setCursor(0, 30);
    display.printf("Valor: %d", analogRead(MQ135_PIN));
    display.display();
    
    delay(100);
  }
  
  // Establecer el valor base como el promedio de las muestras
  mq135BaselineValue = sum / samples;
  
  Serial.printf("Calibración MQ135 completada. Valor base: %d\n", mq135BaselineValue);
  
  // Mostrar el resultado de la calibración
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Calibracion completa");
  display.setCursor(0, 10);
  display.printf("Valor base: %d", mq135BaselineValue);
  display.setCursor(0, 30);
  display.println("Iniciando operacion normal...");
  display.display();
  
  delay(2000);  // Mostrar el resultado por 2 segundos
}

bool updateSensors() {
  try {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
      return false;
    }
    
    int soilValue = readAnalogAverage(SOIL_SENSOR_PIN);
    int soilMoisture = 100 - map(soilValue, 0, 4095, 0, 100);
    
    int lightValue = readAnalogAverage(LIGHT_SENSOR_PIN);
    int lightLevel = 100 - map(lightValue, 0, 4095, 0, 100);
    
    // Leer el valor del MQ135 con más muestras para mayor precisión
    int airQualityValue = readAnalogAverage(MQ135_PIN, MQ135_SAMPLES);
    
    // Verificar si ya pasó el periodo de calentamiento
    if (millis() - startTime >= WARMUP_TIME && !systemWarmedUp) {
      systemWarmedUp = true;
      Serial.println("\n=================================");
      Serial.println("¡PERIODO DE CALENTAMIENTO COMPLETADO!");
      Serial.println("El sistema ahora está en pleno funcionamiento");
      Serial.println("=================================\n");
      
      // Calibrar el sensor MQ135 justo después del periodo de calentamiento
      calibrateMQ135();
    }
    
    bool tempOptimal = isTemperatureOptimal(temperature);
    bool soilOptimal = isSoilMoistureOptimal(soilMoisture);
    bool airQualityGood = isAirQualityGood(airQualityValue);
    
    // Durante el periodo de calentamiento, consideramos que el sistema está "normal"
    bool systemOptimal = !systemWarmedUp || (tempOptimal && soilOptimal && airQualityGood);
    
    // Actualizar LED RGB según el estado del sistema
    updateRGBLed(systemOptimal);
    
    // Controlar el riego y obtener el estado actual
    // Durante el calentamiento, la bomba permanece apagada
    bool isIrrigating = controlIrrigation(soilMoisture);
    
    // Controlar el buzzer según la calidad del aire
    // Durante el calentamiento, el buzzer permanece apagado
    controlBuzzer(!airQualityGood);
    
    // Comprobar si ha habido cambios significativos en los sensores
    valuesChanged = isChangeSignificant(lastTemperature, temperature, TEMP_CHANGE_THRESHOLD) ||
                    isChangeSignificant(lastHumidity, humidity, HUMIDITY_CHANGE_THRESHOLD) ||
                    isChangeSignificant(lastSoilMoisture, soilMoisture, SOIL_CHANGE_THRESHOLD) ||
                    isChangeSignificant(lastLightLevel, lightLevel, LIGHT_CHANGE_THRESHOLD) ||
                    isChangeSignificant(lastAirQuality, airQualityValue, AIR_QUALITY_CHANGE_THRESHOLD) ||
                    (lastIrrigationState != isIrrigating);
    
    // Actualizar la pantalla OLED en cada iteración
    // Quitamos la condición para que siempre se actualice la pantalla
    updateDisplay(temperature, humidity, soilMoisture, lightLevel, airQualityValue, isIrrigating);
    
    // Publicar datos si MQTT está conectado y ya pasó el periodo de calentamiento
    // CAMBIO AQUÍ: Publicar siempre, sin importar si hay cambios significativos
    if (systemWarmedUp && client.connected()) {
      // Publicar temperatura en cada lectura
      if (client.publish(MQTT_TOPIC_TEMP, String(temperature).c_str())) {
        Serial.printf("Temperatura %.1f°C publicada en %s\n", temperature, MQTT_TOPIC_TEMP);
      } else {
        Serial.printf("Error al publicar temperatura en %s\n", MQTT_TOPIC_TEMP);
      }
      
      // Publicar humedad del suelo en cada lectura
      if (client.publish(MQTT_TOPIC_SOIL, String(soilMoisture).c_str())) {
        Serial.printf("Humedad del suelo %d%% publicada en %s\n", soilMoisture, MQTT_TOPIC_SOIL);
      } else {
        Serial.printf("Error al publicar humedad del suelo en %s\n", MQTT_TOPIC_SOIL);
      }
      
      // Publicar calidad del aire en cada lectura
      if (client.publish(MQTT_TOPIC_GAS, String(airQualityValue).c_str())) {
        Serial.printf("Calidad del aire %d publicada en %s\n", airQualityValue, MQTT_TOPIC_GAS);
      } else {
        Serial.printf("Error al publicar calidad del aire en %s\n", MQTT_TOPIC_GAS);
      }
    }
    
    // Mostrar información de estado en el monitor serial
    if (!systemWarmedUp) {
      unsigned long remainingTime = (WARMUP_TIME - (millis() - startTime)) / 1000;
      Serial.printf("CALENTANDO SENSORES - Tiempo restante: %lu segundos\n", remainingTime);
    }
    
    Serial.printf("Temp: %.1f°C, Hum: %.0f%%, Suelo: %d%%, Luz: %d%%, Aire: %d", 
                  temperature, humidity, soilMoisture, lightLevel, airQualityValue);
    
    // Mostrar información adicional del MQ135 si está calibrado
    if (mq135BaselineValue != -1) {
      Serial.printf(" (base: %d, dif: %d)", mq135BaselineValue, airQualityValue - mq135BaselineValue);
    } else {
      Serial.print(" (sin calibrar)");
    }
    
    Serial.printf(", Riego: %s, Estado: %s\n",
                  isIrrigating ? "ENCENDIDO" : "APAGADO",
                  systemWarmedUp ? "OPERACIONAL" : "CALENTANDO");
    
    // Guardar los valores actuales como últimos valores
    lastTemperature = temperature;
    lastHumidity = humidity;
    lastSoilMoisture = soilMoisture;
    lastLightLevel = lightLevel;
    lastAirQuality = airQualityValue;
    lastIrrigationState = isIrrigating;
    
    return true;
  }
  catch (...) {
    Serial.println("Error reading sensors");
    return false;
  }
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
  dht.begin();
  
  initializeSystem();
  
  if (!connectWiFi()) {
    Serial.println("Continuing without WiFi connectivity...");
  } else {
    subscribeMQTT();
  }
  
  // Inicializar el timestamp de la última lectura
  lastSensorReadTime = millis();
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Verificar si es tiempo de leer los sensores
  if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = currentMillis;
    
    if (!updateSensors()) {
      failCount++;
    } else {
      failCount = 0;
    }
    
    if (failCount >= 5) {
      Serial.println("Too many consecutive errors. Restarting system...");
      initializeSystem();
      if (connectWiFi()) {
        subscribeMQTT();
      }
      failCount = 0;
    }
    
    // Mostrar mensaje de estado en el monitor serial
    Serial.printf("Estado del sistema: %s\n", 
                  (failCount > 0) ? "Con errores" : "Normal");
    Serial.println("--------------------------------------");
  }
  
  // Verificar y manejar la conexión MQTT
  if (!client.connected() && WiFi.status() == WL_CONNECTED) {
    Serial.println("Reconectando al broker MQTT...");
    if (subscribeMQTT()) {
      Serial.println("Reconexión exitosa");
    } else {
      Serial.println("Reconexión fallida");
    }
  }
  
  if (client.connected()) {
    client.loop();
  }
  
  // Pequeña pausa para no sobrecargar el CPU
  delay(100);
}