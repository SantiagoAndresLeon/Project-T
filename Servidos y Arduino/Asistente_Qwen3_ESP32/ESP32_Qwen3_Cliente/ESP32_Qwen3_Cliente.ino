
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebSocketsClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

// ===== CONFIGURACION =====
const char *WIFI_SSID = "Principe";
const char *WIFI_PASSWORD = "Prin$2025@";
const char *SERVER_IP = "192.168.1.4";
const uint16_t SERVER_PORT = 8000;

const char* DEVICE_ID = "ESP32_001";
const char* DEVICE_TOKEN = "demo-local-123";

// ===== PINES =====
constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;

constexpr int MIC_BCLK = 26;
constexpr int MIC_WS = 25;
constexpr int MIC_SD = 33;

constexpr int RECORD_SECONDS = 5;
constexpr int SAMPLE_RATE = 16000;

// ===== OBJETOS =====
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WebSocketsClient websocket;

bool oledOK = false;
bool micOK = false;
bool wsConnected = false;

String screenTitle = "Mini IA";
String screenBody = "Iniciando...";
String textLines[55];

int lineCount = 0;
int currentPage = 0;

unsigned long lastPageChange = 0;
unsigned long lastDebug = 0;

// ===== TEXTO OLED =====
String simplifyUTF8(String value) {
  value.replace("á", "a");
  value.replace("é", "e");
  value.replace("í", "i");
  value.replace("ó", "o");
  value.replace("ú", "u");
  value.replace("ü", "u");

  value.replace("Á", "A");
  value.replace("É", "E");
  value.replace("Í", "I");
  value.replace("Ó", "O");
  value.replace("Ú", "U");

  value.replace("ñ", "n");
  value.replace("Ñ", "N");
  value.replace("¿", "");
  value.replace("¡", "");

  return value;
}

// ===== PANTALLA =====
void renderScreen() {
  if (!oledOK) return;

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 0);
  oled.println(screenTitle.substring(0, 20));

  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  int first = currentPage * 6;

  for (int row = 0;
       row < 6 && first + row < lineCount;
       ++row) {
    oled.setCursor(0, 14 + row * 8);
    oled.print(textLines[first + row]);
  }

  oled.display();
}

void show(String title, String body) {
  screenTitle = simplifyUTF8(title);
  screenBody = simplifyUTF8(body);

  if (screenBody.length() > 800) {
    screenBody = screenBody.substring(0, 800);
  }

  lineCount = 0;
  String remaining = screenBody;

  while (remaining.length() > 0 &&
         lineCount < 55) {

    if (remaining[0] == '\n') {
      remaining.remove(0, 1);
      continue;
    }

    int maxLen = min(21, (int)remaining.length());
    int cut = maxLen;

    int newlineIndex = remaining.indexOf('\n');

    if (newlineIndex >= 0 &&
        newlineIndex < cut) {
      cut = newlineIndex;
    }
    else if ((int)remaining.length() > 21) {
      int lastSpace = remaining.lastIndexOf(' ', 20);
      if (lastSpace >= 9) cut = lastSpace;
    }

    if (cut < 1) cut = 1;

    textLines[lineCount++] =
      remaining.substring(0, cut);

    remaining.remove(0, cut);
    remaining.trim();
  }

  currentPage = 0;
  lastPageChange = millis();
  renderScreen();
}

// ===== WEBSOCKET =====
void onWebSocket(
  WStype_t type,
  uint8_t* payload,
  size_t length
) {
  if (type == WStype_CONNECTED) {

    Serial.println(
      "[WS] Conexion WebSocket establecida."
    );

    wsConnected = true;

    Serial.print("[WS] Identificando: ");
    Serial.println(DEVICE_ID);

    websocket.sendTXT(
      String("HELLO|") +
      DEVICE_ID + "|" +
      DEVICE_TOKEN
    );

    show(
      "ESP32 conectado",
      "Esperando autorizacion..."
    );
  }

  else if (type == WStype_DISCONNECTED) {

    Serial.println(
      "[WS] Desconectado. Reintentando..."
    );

    wsConnected = false;

    show(
      "Sin servidor",
      "Reintentando..."
    );
  }

  else if (type == WStype_ERROR) {

    Serial.println(
      "[WS] Error de WebSocket"
    );

    show(
      "Error WebSocket",
      "Revisa monitor serie"
    );
  }

  else if (type == WStype_TEXT) {

    String packet;
    packet.reserve(length + 1);

    for (size_t i = 0; i < length; i++) {
      packet += (char)payload[i];
    }

    int split = packet.indexOf('|');

    if (split == -1) {
      Serial.println(packet);
      return;
    }

    String kind = packet.substring(0, split);
    String body = packet.substring(split + 1);

    Serial.printf(
      "[%s] %s\n",
      kind.c_str(),
      body.c_str()
    );

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

      Serial.println(
        "[READY] Conectado a Qwen3 y autenticado."
      );

      show(
        "IA preparada",
        "Escribe pregunta o envia v para hablar."
      );
    }
  }
}

// ===== MICROFONO I2S =====
bool initializeMicrophone() {

  i2s_config_t config = {};

  config.mode =
    (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);

  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample =
    I2S_BITS_PER_SAMPLE_32BIT;

  config.channel_format =
    I2S_CHANNEL_FMT_ONLY_LEFT;

  config.communication_format =
    I2S_COMM_FORMAT_STAND_I2S;

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

  return true;
}

// ===== GRABAR VOZ =====
void recordAndSendVoice() {

  if (!wsConnected) {
    Serial.println("[AUDIO] Sin servidor.");
    show("Error", "Conecta al servidor.");
    return;
  }

  if (!micOK) {
    Serial.println("[AUDIO] Error microfono.");
    show("Error", "Microfono no disponible");
    return;
  }

  Serial.println("[AUDIO] Grabando 5 segundos");

  show(
    "Grabando",
    "Habla durante 5 segundos..."
  );

  websocket.sendTXT("START_AUDIO");

  int32_t input[256];
  int16_t output[256];

  int samplesSent = 0;

  int maxSamples =
    SAMPLE_RATE * RECORD_SECONDS;

  while (samplesSent < maxSamples &&
         wsConnected) {

    size_t gotBytes = 0;

    if (i2s_read(
        I2S_NUM_0,
        input,
        sizeof(input),
        &gotBytes,
        pdMS_TO_TICKS(1500)
      ) != ESP_OK || gotBytes == 0) {
      break;
    }

    int count = gotBytes / sizeof(int32_t);

    count = min(
      count,
      maxSamples - samplesSent
    );

    for (int i = 0; i < count; ++i) {
      output[i] = (int16_t)(input[i] >> 16);
    }

    websocket.sendBIN(
      reinterpret_cast<uint8_t*>(output),
      count * sizeof(int16_t)
    );

    samplesSent += count;

    websocket.loop();
    yield();
  }

  if (wsConnected) {
    websocket.sendTXT("END_AUDIO");
  }

  Serial.printf(
    "[AUDIO] Muestras enviadas: %d\n",
    samplesSent
  );

  show(
    "Audio enviado",
    "Esperando transcripcion..."
  );
}

// ===== SETUP =====
void setup() {

  Serial.begin(115200);
  Serial.setTimeout(70);
  delay(300);

  Serial.println();
  Serial.println(
    "=== ESP32 + Qwen3 1.7B ==="
  );

  // Inicializar OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  oledOK = oled.begin(
    SSD1306_SWITCHCAPVCC,
    0x3C
  );

  if (!oledOK) {
    Serial.println("[OLED] No detectada");
  }

  show("Qwen3 + ESP32", "Iniciando...");

  // Inicializar microfono
  micOK = initializeMicrophone();

  if (micOK) {
    Serial.println("[I2S] Microfono OK");
  } else {
    Serial.println("[I2S] Error microfono");
  }

  // WiFi
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

  Serial.print("[WiFi] Gateway: ");
  Serial.println(WiFi.gatewayIP());

  // Diagnostico TCP
  Serial.printf(
    "[TCP] Probando %s:%u\n",
    SERVER_IP,
    SERVER_PORT
  );

  WiFiClient testClient;

  if (testClient.connect(
      SERVER_IP,
      SERVER_PORT
    )) {

    Serial.println(
      "[TCP] Servidor accesible"
    );

    testClient.stop();

  } else {

    Serial.println(
      "[TCP] No se puede conectar"
    );
  }

  // WebSocket
  Serial.printf(
    "[WS] Conectando a ws://%s:%u/ws\n",
    SERVER_IP,
    SERVER_PORT
  );

  websocket.onEvent(onWebSocket);

  websocket.begin(
    SERVER_IP,
    SERVER_PORT,
    "/ws"
  );

  websocket.setReconnectInterval(3000);

  show(
    "WiFi conectado",
    "Esperando conexion Qwen3..."
  );

  Serial.println(
    "[SERIE] Escribe una pregunta o envia v."
  );
}

// ===== LOOP =====
void loop() {

  websocket.loop();

  // Recibir texto desde Monitor Serie
  if (Serial.available()) {

    String question =
      Serial.readStringUntil('\n');

    question.trim();

    if (question == "v" ||
        question == "V") {

      recordAndSendVoice();

    } else if (question.length()) {

      if (wsConnected) {

        Serial.println(
          "[PREGUNTA] Enviando: " + question
        );

        websocket.sendTXT(
          String("Q|") + question
        );

        show(
          "Pregunta enviada:",
          question
        );

      } else {

        Serial.println(
          "[PREGUNTA] Sin conexion WebSocket"
        );

        show(
          "Sin servidor",
          "No se envio la pregunta"
        );
      }
    }
  }

  // Desplazar respuestas largas
  if (lineCount > 6 &&
      millis() - lastPageChange > 3500) {

    currentPage =
      (currentPage + 1) /
      ((lineCount + 5) / 6);

    // Cambiar a la siguiente pagina
    currentPage =
      (currentPage == 0)
      ? 1 % ((lineCount + 5) / 6)
      : currentPage;

    lastPageChange = millis();
    renderScreen();
  }

  // Diagnostico cada 15 segundos
  if (millis() - lastDebug > 15000) {

    lastDebug = millis();

    Serial.printf(
      "[ESTADO] WiFi=%s | WebSocket=%s | RAM=%u\n",
      WiFi.status() == WL_CONNECTED
        ? "OK" : "DESCONECTADO",
      wsConnected
        ? "CONECTADO" : "DESCONECTADO",
      (unsigned)ESP.getFreeHeap()
    );
  }
}
