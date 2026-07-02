import asyncio
import websockets
import wave
from datetime import datetime
from pynput import keyboard

from faster_whisper import WhisperModel
import requests
import pyttsx3

HOST = "0.0.0.0"
PORT = 8080

SAMPLE_RATE = 16000
CHANNELS = 1
SAMPWIDTH = 2  # 16-bit PCM

WHISPER_MODEL = "base"
OLLAMA_MODEL = "llama3.1:8b"

# Estado global
is_recording = False
buffer = bytearray()
ws_actual = None  # para poder responderle al cliente

# Cargamos Whisper 1 sola vez (más rápido y evita recargar cada vez)
whisper_model = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8")


def nombre_archivo():
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"grabacion_{ts}.wav"


def guardar_wav(path: str, pcm_bytes: bytes) -> bool:
    if not pcm_bytes:
        print("⚠️ No hay audio para guardar.")
        return False

    with wave.open(path, "wb") as wav:
        wav.setnchannels(CHANNELS)
        wav.setsampwidth(SAMPWIDTH)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm_bytes)

    return True


def transcribir(audio_path: str) -> str:
    print("📝 Transcribiendo con Whisper...")
    segments, _ = whisper_model.transcribe(audio_path, language="es")
    texto = " ".join([s.text.strip() for s in segments]).strip()
    print("👤 DIJISTE:", texto)
    return texto


def preguntar_ollama(texto: str) -> str:
    print("🤖 Generando respuesta (Ollama local)...")
    payload = {
        "model": OLLAMA_MODEL,
        "messages": [
            {"role": "system", "content": "Eres un peluche grosero."},
            {"role": "user", "content": texto}
        ],
        "stream": False
    }
    r = requests.post("http://localhost:11434/api/chat", json=payload, timeout=120)
    r.raise_for_status()
    respuesta = r.json()["message"]["content"].strip()
    print("🧸 RESPUESTA:", respuesta)
    return respuesta


def hablar(texto: str):
    print("🗣️ Hablando...")
    tts = pyttsx3.init()
    tts.setProperty("rate", 175)
    tts.say(texto)
    tts.runAndWait()


async def pipeline_stt_llm_tts(path_wav: str):
    """
    Ejecuta: WAV -> Whisper -> Ollama -> TTS
    Se corre como tarea async para no congelar el servidor.
    """
    global ws_actual

    texto = transcribir(path_wav)

    if not texto:
        print("⚠️ No detecté texto. Intenta hablar más claro.")
        if ws_actual:
            try:
                await ws_actual.send("STT_EMPTY")
            except:
                pass
        return

    respuesta = preguntar_ollama(texto)
    hablar(respuesta)

    # Opcional: enviar respuesta al ESP32 como texto
    if ws_actual:
        try:
            await ws_actual.send(f"AI:{respuesta}")
        except:
            pass


async def toggle_recording():
    global is_recording, buffer, ws_actual

    if not is_recording:
        buffer = bytearray()
        is_recording = True
        print("🔴 Grabación INICIADA (tecla G).")

        if ws_actual:
            try:
                await ws_actual.send("REC_START")
            except:
                pass

    else:
        is_recording = False
        data = bytes(buffer)
        path = nombre_archivo()

        ok = guardar_wav(path, data)
        if ok:
            print(f"✅ Guardado: {path} ({len(data)} bytes)")

            if ws_actual:
                try:
                    await ws_actual.send(f"REC_STOP:{path}")
                except:
                    pass

            # ✅ AQUÍ está la unión: después de guardar, corre el pipeline completo
            asyncio.create_task(pipeline_stt_llm_tts(path))

        else:
            print("⚠️ Grabación detenida pero no se guardó nada.")


def iniciar_listener_teclado(loop: asyncio.AbstractEventLoop):
    def on_press(key):
        try:
            if key.char and key.char.lower() == "g":
                loop.call_soon_threadsafe(lambda: asyncio.create_task(toggle_recording()))
        except AttributeError:
            pass

    listener = keyboard.Listener(on_press=on_press)
    listener.daemon = True
    listener.start()


async def handler(websocket):
    global ws_actual, buffer, is_recording
    ws_actual = websocket
    print("🎧 Cliente conectado. Presiona 'G' para iniciar/detener.")

    try:
        async for message in websocket:
            if isinstance(message, (bytes, bytearray)):
                if is_recording:
                    buffer.extend(message)
            else:
                print("Texto:", message)

    except websockets.ConnectionClosed:
        print("❌ Cliente desconectado.")
    finally:
        if ws_actual == websocket:
            ws_actual = None
        if is_recording:
            print("⚠️ Cliente se desconectó mientras grababas. Se cancela grabación.")
            is_recording = False
            buffer = bytearray()


async def main():
    loop = asyncio.get_running_loop()
    iniciar_listener_teclado(loop)

    async with websockets.serve(handler, HOST, PORT, max_size=None):
        print(f"✅ Servidor WebSocket en ws://{HOST}:{PORT}")
        print("⌨️ Presiona 'G' para iniciar/detener la grabación.")
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
