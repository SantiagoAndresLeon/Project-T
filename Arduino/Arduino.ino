#include <WiFi.h>
#include <WebSocketsClient.h>
#include "driver/i2s.h"

// ========= WIFI =========
const char* WIFI_SSID = "FAM_ROPINZON 2.4";
const char* WIFI_PASS = "1001297_JORMAMP";

// IP de tu PC (servidor) y puerto
const char* WS_HOST = "192.168.40.21";   // <-- CAMBIA ESTO 192.168.1.33
const uint16_t WS_PORT = 8080;
const char* WS_PATH = "/";

#define I2S_PORT I2S_NUM_0
#define PIN_BCLK 26
#define PIN_WS   25
#define PIN_SD   33

static inline int16_t to_pcm16(int32_t s32) {
  int32_t s = s32 >> 8;
  if (s > 32767) s = 32767;
  if (s < -32768) s = -32768;
  return (int16_t)s;
}

WebSocketsClient ws;

void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("WS conectado");
  }
  if (type == WStype_DISCONNECTED) {
    Serial.println("WS desconectado");
  }
}

static void i2s_install() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = PIN_BCLK,
    .ws_io_num = PIN_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_SD
  };

  i2s_driver_install(I2S_PORT, &config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
}

void setup() {
  Serial.begin(115200);

  i2s_install();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  Serial.println("WiFi conectado");

  ws.begin(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(2000);
}

void loop() {
  ws.loop();

  const int N = 256;
  int32_t s32[N];
  size_t read = 0;

  if (i2s_read(I2S_PORT, s32, sizeof(s32), &read, 10) == ESP_OK && read > 0) {
    int samples = read / 4;
    uint8_t buffer[512];
    for (int i = 0; i < samples; i++) {
      int16_t s16 = to_pcm16(s32[i]);
      buffer[2*i]   = s16 & 0xFF;
      buffer[2*i+1] = (s16 >> 8) & 0xFF;
    }
    ws.sendBIN(buffer, samples * 2);
  }
}
