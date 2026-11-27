
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebSocketsClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include <math.h>

// ===== CONFIGURACION DEL SERVIDOR =====

const char* WIFI_SSID = "Principe";
const char* WIFI_PASSWORD = "Prin$2025@";
const char* SERVER_IP = "192.168.1.4";
const uint16_t SERVER_PORT = 8000;

const char* DEVICE_ID = "ESP32_001";
const char* DEVICE_TOKEN = "demo-local-123";

// ===== PINES =====

constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;

constexpr int MIC_BCLK = 26;
constexpr int MIC_WS = 25;
constexpr int MIC_SD = 33;

constexpr int SAMPLE_RATE = 16000;
constexpr int RECORD_SECONDS = 5;
constexpr int AUDIO_GAIN = 8;

// ===== PANTALLA 128 x 32 =====

constexpr int OLED_LINES_PER_PAGE = 2;
constexpr int OLED_CHARS_PER_LINE = 21;

Adafruit_SSD1306 oled(128, 32, &Wire, -1);
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

// ===== NORMALIZAR TEXTO =====

String simplifyUTF8(String value) {
  value.replace("á", "a");
  value.replace("é", "e");
  value.replace("í", "i");
  value.replace("ó", "o");
  value.replace("ú", "u");
  value.replace("ü", "u");
  value.replace("ñ", "n");

  value.replace("Á", "A");
  value.replace("É", "E");
  value.replace("Í", "I");
  value.replace("Ó", "O");
  value.replace("Ú", "U");
  value.replace("Ñ", "N");

  value.replace("¿", "");
  value.replace("¡", "");

  return value;
}

// ===== DIVIDIR TEXTO EN LINEAS =====

void addWrappedText(String paragraph) {
  paragraph.trim();
  String line = "";

  while (paragraph.length() > 0 && lineCount < 100) {
    paragraph.trim();
    if (paragraph.length() == 0) break;

    int split = paragraph.indexOf(' ');

    String word = split < 0
      ? paragraph
      : paragraph.substring(0, split);

    paragraph = split < 0
      ? ""
      : paragraph.substring(split + 1);

    while (word.length() > OLED_CHARS_PER_LINE &&
           lineCount < 100) {
      if (line.length()) {
        textLines[lineCount++] = line;
        line = "";
      }

      if (lineCount < 100) {
        textLines[lineCount++] =
          word.substring(0, OLED_CHARS_PER_LINE);

        word.remove(0, OLED_CHARS_PER_LINE);
      }
    }

    if (!word.length()) continue;

    if (!line.length()) {
      line = word;
    } else if (line.length() + 1 + word.length()
               <= OLED_CHARS_PER_LINE) {
      line += " " + word;
    } else {
      textLines[lineCount++] = line;
      line = word;
    }
  }

  if (line.length() && lineCount < 100) {
    textLines[lineCount++] = line;
  }
}

// ===== DIBUJAR PANTALLA =====

void renderScreen() {
  if (!oledOK) return;

  oled.clearDisplay();

  // Fuente original de Adafruit
  oled.setFont(nullptr);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);

  // Titulo
  oled.setCursor(0, 0);
  oled.print(screenTitle.substring(0, 21));

  // Separador
  oled.drawFastHLine(0, 9, 128, SSD1306_WHITE);

  // Dos lineas de respuesta
  int firstLine = currentPage * OLED_LINES_PER_PAGE;

  for (int i = 0; i < OLED_LINES_PER_PAGE; ++i) {
    int index = firstLine + i;

    if (index < lineCount) {
      oled.setCursor(0, 12 + i * 9);
      oled.print(textLines[index]);
    }
  }

  oled.display();
}

// ===== ACTUALIZAR TEXTO =====

void show(String title, String body) {
  screenTitle = simplifyUTF8(title);
  screenBody = simplifyUTF8(body);

  if (screenBody.length() > 900) {
    screenBody = screenBody.substring(0, 900);
  }

  lineCount = 0;
  int begin = 0;

  while (begin < (int)screenBody.length() &&
         lineCount < 100) {
    int end = screenBody.indexOf('\n', begin);

    if (end < 0) {
      end = screenBody.length();
    }

    addWrappedText(
      screenBody.substring(begin, end)
    );

    begin = end + 1;
  }

  currentPage = 0;
  lastPageChange = millis();

  renderScreen();
}

// ===== COMUNICACION WEBSOCKET =====

void onWebSocket(WStype_t type,
                 uint8_t* payload,
                 size_t length) {

  if (type == WStype_CONNECTED) {
    Serial.println("[WS] Conectado");
    wsConnected = true;

    websocket.sendTXT(
      String("HELLO|") + DEVICE_ID + "|" + DEVICE_TOKEN
    );

    show("ESP32 conectado", "Autenticando...");
  }

  else if (type == WStype_DISCONNECTED) {
    wsConnected = false;
    Serial.println("[WS] Desconectado");
    show("Sin servidor", "Reintentando...");
  }

  else if (type == WStype_ERROR) {
    Serial.println("[WS] Error");
    show("Error WebSocket", "Revisa conexion");
  }

  else if (type == WStype_TEXT) {
    String packet;
    packet.reserve(length + 1);

    for (size_t i = 0; i < length; ++i) {
      packet += (char)payload[i];
    }

    int split = packet.indexOf('|');
    if (split < 0) return;

    String kind = packet.substring(0, split);
    String body = packet.substring(split + 1);

    Serial.printf("[%s] %s\n",
                  kind.c_str(), body.c_str());

    if (kind == "AI") {
      show("Qwen3 responde:", body);
    }
    else if (kind == "HEARD") {
      show("Tu pregunta:", body);
    }
    else if (kind == "STATE") {
      show("Procesando...", body);
    }
    else if (kind == "ERROR") {
      show("Error", body);
    }
    else if (kind == "READY") {
      show("IA preparada",
           "Escribe pregunta o envia v para hablar.");

      Serial.println("[READY] Qwen3 conectado");
    }
  }
}

// ===== CONFIGURACION MICROFONO =====

bool initializeMicrophone() {
  i2s_config_t config = {};

  config.mode =
    (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);

  config.sample_rate = SAMPLE_RATE;

  config.bits_per_sample =
    I2S_BITS_PER_SAMPLE_32BIT;

  config.channel_format =
    I2S_CHANNEL_FMT_ONLY_RIGHT;

  config.communication_format =
    I2S_COMM_FORMAT_I2S;

  config.intr_alloc_flags =
    ESP_INTR_FLAG_LEVEL1;

  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;

  if (i2s_driver_install(
        I2S_NUM_0, &config, 0, nullptr
      ) != ESP_OK) {
    return false;
  }

  i2s_pin_config_t pins = {};

  pins.bck_io_num = MIC_BCLK;
  pins.ws_io_num = MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_SD;

  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    return false;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

// ===== GRABACION Y ENVIO DE AUDIO =====

void recordAndSendVoice() {
  if (!wsConnected) {
    show("Error", "Sin servidor");
    return;
  }

  if (!micOK) {
    show("Error", "Microfono no disponible");
    return;
  }

  show("Grabando", "Habla ahora...");
  Serial.println("[AUDIO] Grabando...");

  websocket.sendTXT("START_AUDIO");

  int32_t input[256];
  int16_t output[256];

  const int maxSamples =
    SAMPLE_RATE * RECORD_SECONDS;

  int samplesSent = 0;
  int clips = 0;
  int peak = 0;

  double sumSquares = 0;

  while (samplesSent < maxSamples && wsConnected) {
    size_t gotBytes = 0;

    esp_err_t status = i2s_read(
      I2S_NUM_0,
      input,
      sizeof(input),
      &gotBytes,
      pdMS_TO_TICKS(1500)
    );

    if (status != ESP_OK || gotBytes == 0) {
      Serial.println("[AUDIO] Error I2S");
      break;
    }

    int count = min(
      (int)(gotBytes / sizeof(int32_t)),
      maxSamples - samplesSent
    );

    for (int i = 0; i < count; ++i) {
      int32_t value =
        (input[i] >> 16) * AUDIO_GAIN;

      if (value > 32767) {
        value = 32767;
        clips++;
      }

      if (value < -32768) {
        value = -32768;
        clips++;
      }

      output[i] = (int16_t)value;

      int absValue = value < 0 ? -value : value;
      if (absValue > peak) peak = absValue;

      sumSquares += (double)value * value;
    }

    bool sent = websocket.sendBIN(
      reinterpret_cast<uint8_t*>(output),
      count * sizeof(int16_t)
    );

    if (!sent) {
      Serial.println("[AUDIO] Error enviando datos");
      break;
    }

    samplesSent += count;
    websocket.loop();
    yield();
  }

  if (wsConnected) {
    websocket.sendTXT("END_AUDIO");
  }

  float rms = samplesSent > 0
    ? sqrt(sumSquares / samplesSent)
    : 0;

  Serial.printf(
    "[AUDIO] Muestras: %d/%d\n",
    samplesSent, maxSamples
  );

  Serial.printf(
    "[AUDIO] Pico=%d RMS=%.1f Recortes=%d\n",
    peak, rms, clips
  );

  show("Audio enviado", "Esperando Whisper...");
}

// ===== INICIALIZACION =====

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(70);
  delay(300);

  Serial.println("=== ESP32 + Qwen3 1.7B ===");

  Wire.begin(OLED_SDA, OLED_SCL);

  oledOK = oled.begin(
    SSD1306_SWITCHCAPVCC, 0x3C
  );

  if (!oledOK) {
    Serial.println("[OLED] No detectada");
  }

  show("Qwen3 + ESP32", "Iniciando...");

  micOK = initializeMicrophone();

  Serial.println(
    micOK ? "[I2S] OK" : "[I2S] Error"
  );

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[WiFi] Conectando");

  while (WiFi.status() != WL_CONNECTED) {
    delay(350);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  WiFiClient testClient;

  if (testClient.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("[TCP] Servidor accesible");
    testClient.stop();
  } else {
    Serial.println("[TCP] Sin conexion");
  }

  websocket.onEvent(onWebSocket);

  websocket.begin(
    SERVER_IP,
    SERVER_PORT,
    "/ws"
  );

  websocket.setReconnectInterval(3000);

  show("WiFi conectado", "Esperando Qwen3...");
}

// ===== BUCLE PRINCIPAL =====

void loop() {
  websocket.loop();

  if (Serial.available()) {
    String question =
      Serial.readStringUntil('\n');

    question.trim();

    if (question == "v" || question == "V") {
      recordAndSendVoice();
    }
    else if (question.length()) {
      if (wsConnected) {
        websocket.sendTXT(
          String("Q|") + question
        );

        show("Pregunta enviada:", question);
      }
      else {
        show("Sin servidor", "No conectado");
      }
    }
  }

  // Cambiar pagina cada 3.5 segundos
  int totalPages =
    (lineCount + OLED_LINES_PER_PAGE - 1) /
    OLED_LINES_PER_PAGE;

  if (totalPages > 1 &&
      millis() - lastPageChange > 3500) {

    currentPage =
      (currentPage + 1) % totalPages;

    lastPageChange = millis();
    renderScreen();
  }

  if (millis() - lastDebug > 15000) {
    lastDebug = millis();

    Serial.printf(
      "[ESTADO] WiFi=%s WebSocket=%s RAM=%u\n",
      WiFi.status() == WL_CONNECTED
        ? "OK" : "DESCONECTADO",
      wsConnected
        ? "CONECTADO" : "DESCONECTADO",
      (unsigned)ESP.getFreeHeap()
    );
  }
}
