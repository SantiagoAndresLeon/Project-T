"""Prototipo local: ESP32 --WebSocket--> Whisper CPU --Ollama Qwen3--> OLED.
Ejecutar: python -m uvicorn servidor:app --host 0.0.0.0 --port 8000
"""
import asyncio
import os
import re
import wave
from pathlib import Path
from collections import defaultdict
from functools import lru_cache

import httpx
import numpy as np
from fastapi import FastAPI, WebSocket, WebSocketDisconnect

MODEL = os.getenv("OLLAMA_MODEL", "qwen3:1.7b")
OLLAMA_API = os.getenv("OLLAMA_API", "http://127.0.0.1:11434/api/chat")
DEVICE_TOKEN = os.getenv("DEVICE_TOKEN", "demo-local-123")  # SOLO red local
ASR_MODEL = os.getenv("ASR_MODEL", "base")  # base: mejor espanol que tiny, mas lento en CPU
SAVE_DEBUG_WAV = os.getenv("SAVE_DEBUG_WAV", "1") == "1"  # Guarda ultimo audio por dispositivo
MAX_AUDIO_BYTES = 16_000 * 2 * 7  # maximo 7 segundos de audio PCM16
MAX_HISTORY_MESSAGES = 12  # 6 intercambios

app = FastAPI(title="Servidor de IA para ESP32")
memories: dict[str, list[dict]] = defaultdict(list)
device_locks: dict[str, asyncio.Lock] = defaultdict(asyncio.Lock)
model_semaphore = asyncio.Semaphore(1)  # 1 generacion inicialmente

PERSONALITIES = {
    "ESP32_001": "Eres Luna, una asistente amistosa, curiosa y paciente.",
    "ESP32_002": "Eres Atlas, un asistente preciso, tranquilo y analitico.",
    "ESP32_003": "Eres Kira, una asistente alegre, ingeniosa y muy conversadora.",
}


@lru_cache(maxsize=1)
def whisper_model():
    # Primera llamada descarga el modelo Whisper; despues lo reutiliza localmente.
    from faster_whisper import WhisperModel
    return WhisperModel(ASR_MODEL, device="cpu", compute_type="int8")


def transcribe_pcm(data: bytes) -> str:
    if len(data) < 16_000:
        print(f"[WHISPER] Audio insuficiente: {len(data)} bytes", flush=True)
        return ""
    audio = np.frombuffer(data, dtype="<i2").astype(np.float32) / 32768.0
    peak = float(np.max(np.abs(audio)))
    rms = float(np.sqrt(np.mean(audio ** 2)))
    print(f"[WHISPER] Muestras={len(audio)} duracion={len(audio)/16000:.2f}s "
          f"pico={peak:.5f} RMS={rms:.5f}", flush=True)
    if peak < 0.003 or rms < 0.0003:
        print("[WHISPER] Senal demasiado baja. Revisar cableado/ganancia.", flush=True)
        return ""
    audio -= np.mean(audio)
    # Normalizacion moderada cuando llega una voz demasiado baja.
    if 0 < rms < 0.05:
        gain = min(8.0, 0.07 / rms)
        audio = np.clip(audio * gain, -1.0, 1.0)
        print(f"[WHISPER] Ganancia software x{gain:.2f}", flush=True)
    segments, _ = whisper_model().transcribe(
        audio,
        language="es",
        beam_size=3,
        vad_filter=False,
        condition_on_previous_text=False,
    )
    result = " ".join(segment.text.strip() for segment in segments).strip()
    print(f"[WHISPER] Transcripcion: {result!r}", flush=True)
    return result


def save_debug_wav(device_id: str, data: bytes) -> None:
    if not SAVE_DEBUG_WAV or not data:
        return
    name = f"ultima_grabacion_{device_id}.wav"
    path = Path(__file__).resolve().parent / name
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(data)
    print(f"[AUDIO] Archivo de diagnostico: {path}", flush=True)


async def generate_response(device_id: str, question: str) -> str:
    personality = PERSONALITIES.get(
        device_id,
        "Eres un asistente amable, cercano y util. Tienes respuestas naturales.",
    )
    system = (
        personality
        + " Responde siempre en espanol. Estas conectado a una pantalla OLED "
        + "de 128 por 64 pixeles. Da respuestas breves (preferiblemente 1 a 3 frases). "
        + "No afirmes tener acceso a internet. No inventes datos que desconozcas."
    )
    async with device_locks[device_id]:
        history = memories[device_id]
        messages = ([{"role": "system", "content": system}]
                    + history
                    + [{"role": "user", "content": question}])
        async with model_semaphore:
            async with httpx.AsyncClient(timeout=180) as client:
                result = await client.post(
                    OLLAMA_API,
                    json={
                        "model": MODEL,
                        "messages": messages,
                        "stream": False,
                        "think": False,
                        "options": {"temperature": 0.7, "num_predict": 110,
                                    "num_ctx": 2048},
                        "keep_alive": "20m",
                    },
                )
                result.raise_for_status()
                response = result.json()["message"]["content"].strip()
        if not response:
            response = "No tengo una respuesta en este momento."
        history.extend([{"role": "user", "content": question},
                        {"role": "assistant", "content": response}])
        if len(history) > MAX_HISTORY_MESSAGES:
            del history[:-MAX_HISTORY_MESSAGES]
        return response


async def handle_question(ws: WebSocket, device_id: str, text: str):
    text = text.strip()[:600]
    if not text:
        await ws.send_text("ERROR|No escuche una pregunta clara.")
        return
    await ws.send_text("HEARD|" + text)
    await ws.send_text("STATE|Generando respuesta...")
    try:
        reply = await generate_response(device_id, text)
        await ws.send_text("AI|" + reply)
    except (httpx.RequestError, httpx.HTTPStatusError) as exc:
        print("Error de Ollama:", repr(exc))
        await ws.send_text("ERROR|Ollama no responde. Revisa que este abierto.")
    except Exception as exc:
        print("Error de generacion:", repr(exc))
        await ws.send_text("ERROR|Error al generar la respuesta.")


@app.get("/health")
def health():
    return {"status": "ok", "model": MODEL}


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    try:
        hello = await asyncio.wait_for(ws.receive_text(), timeout=10)
        parts = hello.split("|", 2)
        if (len(parts) != 3 or parts[0] != "HELLO"
                or parts[2] != DEVICE_TOKEN
                or re.fullmatch(r"[a-zA-Z0-9_-]{1,32}", parts[1]) is None):
            await ws.send_text("ERROR|Identificacion invalida.")
            await ws.close(code=1008)
            return
        device_id = parts[1]
        print("Conectado:", device_id)
        await ws.send_text("READY|Conectado a Qwen3")
        recording = False
        audio_bytes = bytearray()

        while True:
            message = await ws.receive()
            if message["type"] == "websocket.disconnect":
                break
            payload = message.get("bytes")
            if payload is not None:
                if recording:
                    if len(audio_bytes) + len(payload) <= MAX_AUDIO_BYTES:
                        audio_bytes.extend(payload)
                    else:
                        recording = False
                        audio_bytes.clear()
                        await ws.send_text("ERROR|Audio demasiado largo.")
                continue
            cmd = message.get("text") or ""
            if cmd.startswith("Q|"):
                await handle_question(ws, device_id, cmd[2:])
            elif cmd == "START_AUDIO":
                recording = True
                audio_bytes.clear()
                await ws.send_text("STATE|Recibiendo audio...")
            elif cmd == "END_AUDIO" and recording:
                recording = False
                await ws.send_text("STATE|Transcribiendo audio...")
                try:
                    data = bytes(audio_bytes)
                    print(f"[AUDIO] {device_id}: recibidos {len(data)} bytes", flush=True)
                    await asyncio.to_thread(save_debug_wav, device_id, data)
                    question = await asyncio.to_thread(transcribe_pcm, data)
                    await handle_question(ws, device_id, question)
                except Exception as exc:
                    print("Error Whisper:", repr(exc))
                    await ws.send_text(
                        "ERROR|Whisper no pudo transcribir. Verifica sus librerias."
                    )
                finally:
                    audio_bytes.clear()
            else:
                await ws.send_text("ERROR|Comando desconocido.")
    except (WebSocketDisconnect, asyncio.TimeoutError):
        pass
    except Exception as exc:
        print("WebSocket fallo:", repr(exc))
