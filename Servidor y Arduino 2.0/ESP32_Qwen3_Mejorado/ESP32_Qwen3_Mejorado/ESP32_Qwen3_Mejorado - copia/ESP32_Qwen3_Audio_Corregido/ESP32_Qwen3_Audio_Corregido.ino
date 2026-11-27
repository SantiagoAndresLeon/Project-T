// ESP32-WROOM-32 + SSD1306 128x64 + INMP441
// Cliente para el servidor Python Qwen3 1.7B (servidor.py del proyecto anterior).
// Prueba por texto: escribir una pregunta en el Monitor Serie (115200, Nueva linea).
// Prueba por voz: escribir v para grabar 5 segundos de audio con el INMP441.
// Bibliotecas: Adafruit GFX, Adafruit SSD1306, WebSockets (Markus Sattler/Links2004).

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebSocketsClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include <math.h>

// ======== CONFIGURA LOS DATOS DE TU RED ========
const char* WIFI_SSID = "Principe";
const char* WIFI_PASSWORD = "Prin$2025@";
const char* SERVER_IP = "192.168.1.4"; // IPv4 DEL COMPUTADOR, no del ESP32
const uint16_t SERVER_PORT = 8000;
const char* DEVICE_ID = "ESP32_001";
const char* DEVICE_TOKEN = "demo-local-123"; // Debe coincidir con servidor.py
// ==============================================

constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;
constexpr int MIC_BCLK = 26;  // SCK del INMP441
constexpr int MIC_WS = 25;
constexpr int MIC_SD = 33;
constexpr int RECORD_SECONDS = 5;
constexpr int SAMPLE_RATE = 16000;
// Audio: 8 = ganancia inicial x8 frente al codigo anterior.
// Si hay recorte (CLIP) elevado, prueba 4. Si la senal es baja, prueba 16.
constexpr int AUDIO_GAIN = 8;
// OLED 128x64: caracteres de respuesta de 12x16, 3 lineas por pagina.
constexpr int OLED_TEXT_SIZE = 2;
constexpr int OLED_CHARS_PER_LINE = 10;
constexpr int OLED_LINES_PER_PAGE = 3;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WebSocketsClient websocket;
bool oledOK = false;
bool micOK = false;
bool wsConnected = false;
String screenTitle = "Mini IA";
String screenBody = "Iniciando...";
String textLines[100];
int lineCount = 0;
int currentPage = 0;
unsigned long lastPageChange = 0;
unsigned long lastDebug = 0;

String simplifyUTF8(String value) {
  value.replace("á", "a"); value.replace("é", "e");
  value.replace("í", "i"); value.replace("ó", "o");
  value.replace("ú", "u"); value.replace("ü", "u");
  value.replace("Á", "A"); value.replace("É", "E");
  value.replace("Í", "I"); value.replace("Ó", "O");
  value.replace("Ú", "U"); value.replace("ñ", "n");
  value.replace("Ñ", "N"); value.replace("¿", "");
  value.replace("¡", "");
  return value;
}

void renderScreen() {
  if (!oledOK) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  // Titulo pequeno; cuerpo grande para maximizar espacio.
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(screenTitle.substring(0, 21));
  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  oled.setTextSize(OLED_TEXT_SIZE);
  int first = currentPage * OLED_LINES_PER_PAGE;
  for (int row = 0; row < OLED_LINES_PER_PAGE && first + row < lineCount; row++) {
    oled.setCursor(0, 14 + row * 16);
    oled.print(textLines[first + row]);
  }
  oled.display();
}

void show(String title, String body) {
  screenTitle = simplifyUTF8(title);
  screenBody = simplifyUTF8(body);
  if (screenBody.length() > 900) screenBody = screenBody.substring(0, 900);
  lineCount = 0;
  String remaining = screenBody;
  while (remaining.length() > 0 && lineCount < 100) {
    if (remaining[0] == '\n') { remaining.remove(0, 1); continue; }
    int cut = min(OLED_CHARS_PER_LINE, (int)remaining.length());
    int newlineIndex = remaining.indexOf('\n');
    if (newlineIndex >= 0 && newlineIndex < cut) {
      cut = newlineIndex;
    } else if ((int)remaining.length() > OLED_CHARS_PER_LINE) {
      int space = remaining.lastIndexOf(' ', OLED_CHARS_PER_LINE - 1);
      if (space >= 3) cut = space;
    }
    if (cut < 1) cut = 1;
    textLines[lineCount++] = remaining.substring(0, cut);
    remaining.remove(0, cut);
    remaining.trim();
  }
  currentPage = 0;
  lastPageChange = millis();
  renderScreen();
}

// Este callback se invoca desde websocket.loop().
void onWebSocket(WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("[WS] Conexion WebSocket establecida.");
    wsConnected = true;
    Serial.print("[WS] Enviando identificacion: ");
    Serial.println(DEVICE_ID);
    // No mostramos el token por seguridad.
    websocket.sendTXT(String("HELLO|") + DEVICE_ID + "|" + DEVICE_TOKEN);
    show("ESP32 conectado", "Esperando autorizacion...");
  } else if (type == WStype_DISCONNECTED) {
    Serial.println("[WS] Desconectado. Se reintentara automaticamente.");
    wsConnected = false;
    show("Sin servidor", "Reintentando...");
  } else if (type == WStype_ERROR) {
    Serial.printf("[WS] Error de WebSocket (%u bytes).\n", (unsigned)length);
    show("Error WebSocket", "Revisa monitor serie");
  } else if (type == WStype_TEXT) {
    String packet;
    packet.reserve(length + 1);
    for (size_t i = 0; i < length; i++) packet += (char)payload[i];
    int split = packet.indexOf('|');
    if (split == -1) {
      Serial.print("[WS] Mensaje inesperado: ");
      Serial.println(packet);
      return;
    }
    String kind = packet.substring(0, split);
    String body = packet.substring(split + 1);
    Serial.printf("[%s] %s\n", kind.c_str(), body.c_str());
    if (kind == "AI") show("Qwen3 responde:", body);
    else if (kind == "HEARD") show("Tu pregunta:", body);
    else if (kind == "STATE") show("Procesando...", body);
    else if (kind == "ERROR") show("Error", body);
    else if (kind == "READY") {
      Serial.println("[READY] Conectado a Qwen3 y autenticado.");
      show("IA preparada", "Escribe pregunta o envia v para hablar.");
    }
  }
}

bool initializeMicrophone() {
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  // IMPORTANTE: coincide con prueba_microfono_AbrirSerialPloter.ino
  // En el entorno del usuario la lectura comprobada usa ONLY_RIGHT.
  // Si la conexion L/R = GND y esta configuracion produce silencio,
  // probar ONLY_LEFT y verificar el cable L/R fisico.
  config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
  // La misma variante usada por el programa de microfono funcional.
  config.communication_format = I2S_COMM_FORMAT_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.bck_io_num = MIC_BCLK;
  pins.ws_io_num = MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_SD;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;
  // Igual que el codigo de prueba: vacia el DMA antes de grabar.
  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("[I2S] Configuracion del microfono: ONLY_RIGHT / I2S / 16000 Hz / 32 bits");
  return true;
}

void recordAndSendVoice() {
  if (!wsConnected) {
    Serial.println("[AUDIO] No conectado al servidor.");
    show("Error", "No conectado");
    return;
  }
  if (!micOK) {
    Serial.println("[AUDIO] No se inicializo el microfono.");
    show("Error", "Revisa I2S");
    return;
  }
  Serial.println("[AUDIO] Grabando 5 segundos. Habla cerca del microfono...");
  Serial.println("[AUDIO] Esperado: aprox. 80000 muestras PCM16; observa pico/RMS.");
  show("Grabando", "Habla ahora...");
  websocket.sendTXT("START_AUDIO");

  int32_t input[256];
  int16_t output[256];
  int samplesSent = 0;
  int clips = 0;
  int peak = 0;
  double sumSquares = 0;
  const int maxSamples = SAMPLE_RATE * RECORD_SECONDS;
  unsigned long started = millis();

  while (samplesSent < maxSamples && wsConnected) {
    size_t gotBytes = 0;
    esp_err_t status = i2s_read(I2S_NUM_0, input, sizeof(input), &gotBytes,
                               pdMS_TO_TICKS(1500));
    if (status != ESP_OK || gotBytes == 0) {
      Serial.printf("[AUDIO] Error I2S: %d, bytes: %u\n", status, (unsigned)gotBytes);
      break;
    }
    int count = min((int)(gotBytes / sizeof(int32_t)), maxSamples - samplesSent);
    for (int i = 0; i < count; ++i) {
      // INMP441: 24 bits firmados alineados a la izquierda en slots de 32.
      // Original: >>16. Ahora ganancia x8 equivalente a >>13.
      int32_t value = (input[i] >> 16) * AUDIO_GAIN;
      if (value > 32767) { value = 32767; clips++; }
      if (value < -32768) { value = -32768; clips++; }
      output[i] = (int16_t)value;
      int absValue = value < 0 ? -value : value;
      if (absValue > peak) peak = absValue;
      sumSquares += (double)value * value;
    }
    bool sent = websocket.sendBIN(reinterpret_cast<uint8_t*>(output),
                                  count * sizeof(int16_t));
    if (!sent) {
      Serial.println("[AUDIO] Error enviando un paquete WebSocket.");
      break;
    }
    samplesSent += count;
    websocket.loop();
    yield();
  }
  if (wsConnected) websocket.sendTXT("END_AUDIO");
  float rms = samplesSent > 0 ? sqrt(sumSquares / samplesSent) : 0;
  Serial.printf("[AUDIO] Enviadas: %d/%d muestras | duracion TX: %lu ms\n",
                samplesSent, maxSamples, millis() - started);
  Serial.printf("[AUDIO] pico: %d/32767 | RMS: %.1f | recortes: %d | ganancia: x%d\n",
                peak, rms, clips, AUDIO_GAIN);
  if (samplesSent < maxSamples * 0.9) {
    Serial.println("[AUDIO] ADVERTENCIA: grabacion incompleta; revisar red/I2S.");
  }
  if (peak < 100 || rms < 10.0f) {
    Serial.println("[AUDIO] ALERTA: casi no hay voz en la senal capturada.");
    Serial.println("[AUDIO] Compara ONLY_RIGHT y ONLY_LEFT; confirma L/R fisico.");
  } else if (clips > samplesSent / 100) {
    Serial.println("[AUDIO] ALERTA: excesivo recorte, reduce AUDIO_GAIN a 4.");
  } else {
    Serial.println("[AUDIO] Senal detectada. Si Whisper falla, escuchar WAV del servidor.");
  }
  show("Audio enviado", "Esperando Whisper...");
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(70);
  delay(300);
  Serial.println("\n=== ESP32 + Qwen3 1.7B: DIAGNOSTICO ===");

  Wire.begin(OLED_SDA, OLED_SCL);
  oledOK = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOK) Serial.println("[OLED] No detectada en 0x3C.");
  show("Qwen3 + ESP32", "Iniciando...");

  micOK = initializeMicrophone();
  Serial.println(micOK ? "[I2S] Microfono inicializado." : "[I2S] ERROR al inicializar microfono.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Conectando");
  while (WiFi.status() != WL_CONNECTED) {
    delay(350);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("ESP32 IP: "); Serial.println(WiFi.localIP());
  Serial.print("[WiFi] Gateway: "); Serial.println(WiFi.gatewayIP());

  Serial.printf("[TCP] Probando servidor %s:%u ...\n", SERVER_IP, SERVER_PORT);
  WiFiClient testClient;
  if (testClient.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("[TCP] Servidor accesible.");
    testClient.stop();
  } else {
    Serial.println("[TCP] No se puede conectar. Revisa SERVER_IP y firewall.");
  }

  // El servidor Python original usa el endpoint /ws y requiere HELLO|id|token.
  Serial.printf("[WS] Conectando a ws://%s:%u/ws\n", SERVER_IP, SERVER_PORT);
  websocket.onEvent(onWebSocket);
  websocket.begin(SERVER_IP, SERVER_PORT, "/ws");
  websocket.setReconnectInterval(3000);
  show("WiFi conectado", "Esperando conexion Qwen3...");
  Serial.println("[SERIE] Envia una pregunta; para grabar voz, envia v.");
}

void loop() {
  websocket.loop();

  if (Serial.available()) {
    String question = Serial.readStringUntil('\n');
    question.trim();
    if (question == "v" || question == "V") {
      recordAndSendVoice();
    } else if (question.length()) {
      if (wsConnected) {
        Serial.println("[PREGUNTA] Enviando: " + question);
        websocket.sendTXT(String("Q|") + question);
        show("Pregunta enviada:", question);
      } else {
        Serial.println("[PREGUNTA] No enviada: falta conexion WebSocket.");
        show("Sin servidor", "No se envio la pregunta");
      }
    }
  }

  if (lineCount > OLED_LINES_PER_PAGE && millis() - lastPageChange > 4000) {
    currentPage = (currentPage + 1) % ((lineCount + OLED_LINES_PER_PAGE - 1) / OLED_LINES_PER_PAGE);
    lastPageChange = millis();
    renderScreen();
  }

  if (millis() - lastDebug > 15000) {
    lastDebug = millis();
    Serial.printf("[ESTADO] WiFi=%s | WebSocket=%s | RAM libre=%u bytes\n",
                  WiFi.status() == WL_CONNECTED ? "OK" : "DESCONECTADO",
                  wsConnected ? "CONECTADO" : "DESCONECTADO",
                  (unsigned)ESP.getFreeHeap());
  }
}
