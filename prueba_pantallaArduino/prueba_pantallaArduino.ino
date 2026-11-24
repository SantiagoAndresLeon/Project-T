// Librerias necesarias Adafruit SSD1306, - Adafruit GFX Library


#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ========================================
// CONFIGURACION PANTALLA OLED
// ========================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

#define OLED_SDA 21
#define OLED_SCK 22

#define OLED_RESET -1

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// ========================================
// SETUP
// ========================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println("Iniciando pantalla OLED...");

  // Iniciar comunicacion I2C
  Wire.begin(OLED_SDA, OLED_SCK);

  // Inicializar pantalla
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {

    Serial.println("ERROR: No se pudo iniciar OLED");

    while (true) {
      delay(1000);
    }
  }

  Serial.println("OLED iniciada correctamente");

  // Limpiar pantalla
  display.clearDisplay();

  // Configurar texto
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Primera linea
  display.setCursor(0, 0);
  display.println("CLOSED CAPTION");

  // Segunda linea
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.println("Hola mundo");

  // Mostrar contenido
  display.display();
}

// ========================================
// LOOP
// ========================================

void loop() {
  // No necesitamos actualizar la pantalla
}
