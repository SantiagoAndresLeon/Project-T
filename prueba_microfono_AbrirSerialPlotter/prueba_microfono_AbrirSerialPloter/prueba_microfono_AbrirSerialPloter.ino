#include <Arduino.h>
#include "driver/i2s.h"

// =====================================================
// CONFIGURACIÓN DEL INMP441
// =====================================================

#define I2S_PORT I2S_NUM_0

// Conexiones:
// INMP441 SCK  -> GPIO 26
// INMP441 WS   -> GPIO 25
// INMP441 SD   -> GPIO 33
// INMP441 VDD  -> 3.3V
// INMP441 GND  -> GND
// INMP441 L/R  -> GND

#define PIN_BCLK 26
#define PIN_WS   25
#define PIN_SD   33

#define SAMPLES 256


// =====================================================
// INSTALAR I2S
// =====================================================

void i2s_install() {

  i2s_config_t config = {

    .mode = (i2s_mode_t)(
      I2S_MODE_MASTER |
      I2S_MODE_RX
    ),

    // Frecuencia de muestreo
    .sample_rate = 16000,

    // INMP441 entrega muestras de 24 bits
    // dentro de palabras de 32 bits
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,

    // IMPORTANTE:
    // Con nuestras pruebas vimos que este micrófono
    // funciona correctamente usando RIGHT
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,

    .communication_format = I2S_COMM_FORMAT_I2S,

    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,

    .dma_buf_count = 8,
    .dma_buf_len = 256,

    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };


  // ===================================================
  // CONFIGURACIÓN DE PINES
  // ===================================================

  i2s_pin_config_t pins = {

    // SCK / BCLK
    .bck_io_num = PIN_BCLK,

    // WS / LRCLK
    .ws_io_num = PIN_WS,

    // No estamos enviando audio
    .data_out_num = I2S_PIN_NO_CHANGE,

    // Datos provenientes del INMP441
    .data_in_num = PIN_SD
  };


  // ===================================================
  // INSTALAR DRIVER
  // ===================================================

  esp_err_t result;

  result = i2s_driver_install(
    I2S_PORT,
    &config,
    0,
    NULL
  );


  if (result != ESP_OK) {

    Serial.print("ERROR instalando I2S: ");
    Serial.println(result);

    return;
  }


  // ===================================================
  // CONFIGURAR PINES
  // ===================================================

  result = i2s_set_pin(
    I2S_PORT,
    &pins
  );


  if (result != ESP_OK) {

    Serial.print("ERROR configurando pines: ");
    Serial.println(result);

    return;
  }


  // Limpiar buffer
  i2s_zero_dma_buffer(I2S_PORT);


  Serial.println("I2S configurado correctamente.");
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1500);


  Serial.println();
  Serial.println("===============================");
  Serial.println("     ESP32 + INMP441");
  Serial.println("===============================");
  Serial.println();

  Serial.println("Configuracion:");
  Serial.println("SCK  -> GPIO 26");
  Serial.println("WS   -> GPIO 25");
  Serial.println("SD   -> GPIO 33");
  Serial.println("VDD  -> 3.3V");
  Serial.println("GND  -> GND");
  Serial.println("L/R  -> GND");
  Serial.println();


  // Inicializar micrófono
  i2s_install();


  Serial.println();
  Serial.println("Microfono iniciado.");
  Serial.println("Habla o haz ruido cerca del INMP441.");
  Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  // Buffer para almacenar las muestras
  int32_t samples[SAMPLES];

  size_t bytesRead = 0;


  // ===================================================
  // LEER MICRÓFONO
  // ===================================================

  esp_err_t result = i2s_read(
    I2S_PORT,
    samples,
    sizeof(samples),
    &bytesRead,
    portMAX_DELAY
  );


  if (result != ESP_OK) {
    return;
  }


  if (bytesRead == 0) {
    return;
  }


  // Cantidad de muestras recibidas
  int sampleCount =
    bytesRead / sizeof(int32_t);


  // ===================================================
  // CALCULAR MEDIA / OFFSET DC
  // ===================================================

  int64_t sum = 0;


  for (int i = 0; i < sampleCount; i++) {

    // Convertimos la muestra de 32 bits
    // a aproximadamente 24 bits útiles
    int32_t sample =
      samples[i] >> 8;

    sum += sample;
  }


  int32_t mean =
    sum / sampleCount;


  // ===================================================
  // CALCULAR NIVEL DE SONIDO
  // ===================================================

  int64_t audioSum = 0;


  for (int i = 0; i < sampleCount; i++) {

    int32_t sample =
      samples[i] >> 8;


    // Eliminar offset DC
    sample -= mean;


    // Magnitud de la señal
    audioSum += abs(sample);
  }


  // Nivel promedio de sonido
  int32_t audioLevel =
    audioSum / sampleCount;


  // ===================================================
  // MOSTRAR RESULTADO
  // ===================================================

  Serial.println(audioLevel);


  // Pequeña pausa para que el Serial Plotter
  // sea más fácil de visualizar
  delay(20);
}