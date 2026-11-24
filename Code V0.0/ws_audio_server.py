import asyncio
import websockets
import wave
from datetime import datetime
from pynput import keyboard

HOST = "0.0.0.0"
PORT = 8080

SAMPLE_RATE = 16000
CHANNELS = 1
SAMPWIDTH = 2  # 16-bit PCM

# Estado global
is_recording = False
buffer = bytearray()
ws_actual = None  # para poder responderle al cliente

def nombre_archivo():
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"grabacion_{ts}.wav"

def guardar_wav(path: str, pcm_bytes: bytes):
    if not pcm_bytes:
        print("⚠️ No hay audio para guardar.")
        return False

    with wave.open(path, "wb") as wav:
        wav.setnchannels(CHANNELS)
        wav.setsampwidth(SAMPWIDTH)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm_bytes)

    return True

async def toggle_recording():
    global is_recording, buffer, ws_actual

    if not is_recording:
        # INICIA grabación
        buffer = bytearray()
        is_recording = True
        print("🔴 Grabación INICIADA (tecla G).")

        # opcional: avisar al ESP32
        if ws_actual:
            try:
                await ws_actual.send("REC_START")
            except:
                pass
    else:
        # DETIENE grabación y guarda
        is_recording = False
        data = bytes(buffer)
        path = nombre_archivo()

        ok = guardar_wav(path, data)
        if ok:
            print(f"✅ Guardado: {path} ({len(data)} bytes)")
            # opcional: avisar al ESP32
            if ws_actual:
                try:
                    await ws_actual.send(f"REC_STOP: {path}")
                except:
                    pass
        else:
            print("⚠️ Grabación detenida pero no se guardó nada.")

def iniciar_listener_teclado(loop: asyncio.AbstractEventLoop):
    # pynput corre en hilo aparte; para llamar async usamos call_soon_threadsafe
    def on_press(key):
        try:
            if key.char and key.char.lower() == "g":
                loop.call_soon_threadsafe(lambda: asyncio.create_task(toggle_recording()))
        except AttributeError:
            pass  # teclas especiales

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
                # Solo acumulamos cuando is_recording está activo
                if is_recording:
                    buffer.extend(message)
            else:
                print("Texto:", message)

    except websockets.ConnectionClosed:
        print("❌ Cliente desconectado.")
    finally:
        # si se desconecta en medio de grabación, puedes decidir si guardar o no
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
