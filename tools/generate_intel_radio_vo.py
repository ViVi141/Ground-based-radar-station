#!/usr/bin/env python3
"""Generate GBRS intel-radio VO clips and the ACP that plays them.

Speech clips are band-limited only. Radio hiss lives on a separate long
bed (SOUND_GBRS_HISS) so the carrier does not drop between digits.
"""

from __future__ import annotations

import asyncio
import subprocess
import tempfile
import wave
from pathlib import Path

import imageio_ffmpeg
import numpy as np
from scipy.signal import butter, sosfiltfilt

ADDON_ROOT = Path(__file__).resolve().parents[1]
VO_ROOT = ADDON_ROOT / "Sounds" / "GBRS" / "VO"
SAMPLE_RATE = 48000
GUID_ACP = "69FCEDCEA0060100"
HISS_GUID = "69FCEDCEA0060060"
HISS_EVENT = "SOUND_GBRS_HISS"
HISS_DURATION_S = 24.0
FINAL_MIX = "{B764D803219C775E}Sounds/FinalMix.afm"
MIXER_INPUTS = (
    '"WPN_Handling" "WPN_Shots" "WPN_Explosions" "WNP_BulletHits" "CHAR" '
    '"ENV_AMB_2D" "VEH_Animations" "Impacts" "Dialog" "ENV_Doors" '
    '"VEH_Engine" "VEH_Tires" "VON" "SFX" "SFX_Reverb_OBSOLETE" '
    '"VON_Reverb_Small" "Dialog_Reverb" "Impacts_EXT" "ENV_AMB_3D" '
    '"WPN_SonicCracks" "CHAR_Gear" "PA" "SFX_Reverb_Exterior" '
    '"ENV_AMB_3D_Reverb_Exterior" "SFX_Direct" "SFX_Reverb_Small" '
    '"SFX_Reverb_Medium" "SFX_Reverb_Large" "WPN_Shots_Player" '
    '"Dialog_Reverb_Small" "Dialog_Reverb_Medium" "Dialog_Reverb_Large" '
    '"WPN_TravelingProjectile" "Dialog_Delay_Exterior" "SFX_Reverb_InCabin" '
    '"VON_Reverb_Medium" "VON_Reverb_Large"'
)

# (stem, event suffix after SOUND_GBRS_, guid, spoken text)
US_CLIPS = [
    ("air", "AIR_US", "69FCEDCEA0060001", "Radar contact. Grid."),
    ("wlr", "WLR_US", "69FCEDCEA0060002", "Incoming fire."),
    ("out", "OUT_US", "69FCEDCEA0060003", "Out."),
    ("hdg", "HDG_US", "69FCEDCEA0060040", "Heading."),
    ("alt", "ALT_US", "69FCEDCEA0060041", "Altitude."),
    ("meters", "M_US", "69FCEDCEA0060042", "meters."),
    ("lch", "LCH_US", "69FCEDCEA0060043", "Launch. Grid."),
    ("imp", "IMP_US", "69FCEDCEA0060044", "Impact. Grid."),
    ("eta", "ETA_US", "69FCEDCEA0060045", "E T A."),
    ("sec", "SEC_US", "69FCEDCEA0060046", "seconds."),
    ("now", "NOW_US", "69FCEDCEA0060047", "Now."),
    ("d0", "D0_US", "69FCEDCEA0060010", "zero"),
    ("d1", "D1_US", "69FCEDCEA0060011", "one"),
    ("d2", "D2_US", "69FCEDCEA0060012", "two"),
    ("d3", "D3_US", "69FCEDCEA0060013", "tree"),
    ("d4", "D4_US", "69FCEDCEA0060014", "fower"),
    ("d5", "D5_US", "69FCEDCEA0060015", "fife"),
    ("d6", "D6_US", "69FCEDCEA0060016", "six"),
    ("d7", "D7_US", "69FCEDCEA0060017", "seven"),
    ("d8", "D8_US", "69FCEDCEA0060018", "eight"),
    ("d9", "D9_US", "69FCEDCEA0060019", "niner"),
]

USSR_CLIPS = [
    ("air", "AIR_USSR", "69FCEDCEA0060021", "Контакт радара. Квадрат."),
    ("wlr", "WLR_USSR", "69FCEDCEA0060022", "Обстрел."),
    ("out", "OUT_USSR", "69FCEDCEA0060023", "Приём."),
    ("hdg", "HDG_USSR", "69FCEDCEA0060050", "Курс."),
    ("alt", "ALT_USSR", "69FCEDCEA0060051", "Высота."),
    ("meters", "M_USSR", "69FCEDCEA0060052", "метров."),
    ("lch", "LCH_USSR", "69FCEDCEA0060053", "Пуск. Квадрат."),
    ("imp", "IMP_USSR", "69FCEDCEA0060054", "Падение. Квадрат."),
    ("eta", "ETA_USSR", "69FCEDCEA0060055", "Время."),
    ("sec", "SEC_USSR", "69FCEDCEA0060056", "секунд."),
    ("now", "NOW_USSR", "69FCEDCEA0060057", "Сейчас."),
    ("d0", "D0_USSR", "69FCEDCEA0060030", "ноль"),
    ("d1", "D1_USSR", "69FCEDCEA0060031", "один"),
    ("d2", "D2_USSR", "69FCEDCEA0060032", "два"),
    ("d3", "D3_USSR", "69FCEDCEA0060033", "три"),
    ("d4", "D4_USSR", "69FCEDCEA0060034", "четыре"),
    ("d5", "D5_USSR", "69FCEDCEA0060035", "пять"),
    ("d6", "D6_USSR", "69FCEDCEA0060036", "шесть"),
    ("d7", "D7_USSR", "69FCEDCEA0060037", "семь"),
    ("d8", "D8_USSR", "69FCEDCEA0060038", "восемь"),
    ("d9", "D9_USSR", "69FCEDCEA0060039", "девять"),
]


def wav_relpath(faction_dir: str, stem: str) -> str:
    return f"Sounds/GBRS/VO/{faction_dir}/{stem}.wav"


def write_meta(guid: str, relpath: str, resource_class: str) -> None:
    path = ADDON_ROOT / (relpath + ".meta")
    path.parent.mkdir(parents=True, exist_ok=True)
    body = (
        "MetaFileClass {\n"
        f' Name "{{{guid}}}{relpath}"\n'
        " Configurations {\n"
        f"  {resource_class} PC {{\n"
        "  }\n"
        f"  {resource_class} XBOX_ONE : PC {{\n"
        "  }\n"
        f"  {resource_class} XBOX_SERIES : PC {{\n"
        "  }\n"
        f"  {resource_class} PS4 : PC {{\n"
        "  }\n"
        f"  {resource_class} PS5 : PC {{\n"
        "  }\n"
        f"  {resource_class} HEADLESS : PC {{\n"
        "  }\n"
        " }\n"
        "}\n"
    )
    path.write_text(body, encoding="utf-8", newline="\n")


def trim_silence(samples: np.ndarray, sr: int) -> np.ndarray:
    window = max(1, int(sr * 0.01))
    if samples.size < window:
        return samples
    mag = np.abs(samples)
    env = np.convolve(mag, np.ones(window) / window, mode="same")
    threshold = max(0.02, float(np.max(env)) * 0.04)
    voiced = np.where(env > threshold)[0]
    if voiced.size == 0:
        return samples
    pad = int(sr * 0.012)
    start = max(0, int(voiced[0]) - pad)
    end = min(samples.size, int(voiced[-1]) + pad)
    return samples[start:end]


def fade_edges(samples: np.ndarray, sr: int, fade_s: float = 0.006) -> np.ndarray:
    n = samples.size
    fade = min(n // 4, max(1, int(sr * fade_s)))
    if fade <= 1:
        return samples
    env = np.ones(n, dtype=np.float64)
    env[:fade] = np.linspace(0.0, 1.0, fade)
    env[-fade:] = np.linspace(1.0, 0.0, fade)
    return samples * env


def radio_process(samples: np.ndarray, sr: int) -> np.ndarray:
    if samples.size == 0:
        return samples
    x = samples.astype(np.float64)
    peak = np.max(np.abs(x))
    if peak > 1e-8:
        x = x / peak
    sos = butter(4, [400.0, 3100.0], btype="band", fs=sr, output="sos")
    x = sosfiltfilt(sos, x)
    x = np.tanh(x * 1.35)
    peak = np.max(np.abs(x))
    if peak > 1e-8:
        x = x * (0.72 / peak)
    return fade_edges(x, sr)


def make_hiss(sr: int, duration_s: float) -> np.ndarray:
    n = int(sr * duration_s)
    rng = np.random.default_rng(42)
    x = rng.normal(0.0, 1.0, size=n)
    sos = butter(4, [400.0, 3100.0], btype="band", fs=sr, output="sos")
    x = sosfiltfilt(sos, x)
    peak = np.max(np.abs(x))
    if peak > 1e-8:
        x = x * (0.18 / peak)
    return fade_edges(x, sr, 0.01)


def strip_wav_tail(path: Path) -> None:
    if not path.exists():
        return
    with wave.open(str(path), "rb") as wav_file:
        sr = wav_file.getframerate()
        channels = wav_file.getnchannels()
        width = wav_file.getsampwidth()
        frames = wav_file.readframes(wav_file.getnframes())
    if width != 2:
        return
    pcm = np.frombuffer(frames, dtype=np.int16).astype(np.float64) / 32768.0
    if channels > 1:
        pcm = pcm.reshape(-1, channels)[:, 0]
    mag = np.abs(pcm)
    threshold = max(0.004, float(np.max(mag)) * 0.02)
    voiced = np.where(mag > threshold)[0]
    if voiced.size == 0:
        return
    pad = int(sr * 0.008)
    end = min(pcm.size, int(voiced[-1]) + pad)
    trimmed = fade_edges(pcm[:end], sr, 0.005)
    write_wav(path, trimmed, sr)


def write_wav(path: Path, samples: np.ndarray, sr: int) -> None:
    clipped = np.clip(samples, -1.0, 1.0)
    pcm = (clipped * 32767.0).astype(np.int16)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sr)
        wav_file.writeframes(pcm.tobytes())


def decode_mp3_to_float(mp3_path: Path) -> tuple[np.ndarray, int]:
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    raw_path = mp3_path.with_suffix(".raw")
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(mp3_path),
        "-ac",
        "1",
        "-ar",
        str(SAMPLE_RATE),
        "-f",
        "s16le",
        str(raw_path),
    ]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pcm = np.fromfile(raw_path, dtype=np.int16)
    raw_path.unlink(missing_ok=True)
    return pcm.astype(np.float64) / 32768.0, SAMPLE_RATE


async def synth_clip(text: str, voice: str, dest: Path, force: bool) -> None:
    if dest.exists() and not force:
        return

    import edge_tts

    rate = "-18%"
    pitch = "-8Hz"
    with tempfile.TemporaryDirectory() as tmp:
        mp3_path = Path(tmp) / "clip.mp3"
        communicate = edge_tts.Communicate(text, voice, rate=rate, pitch=pitch)
        await communicate.save(str(mp3_path))
        samples, sr = decode_mp3_to_float(mp3_path)
    samples = trim_silence(samples, sr)
    samples = radio_process(samples, sr)
    write_wav(dest, samples, sr)


async def synth_all() -> None:
    jobs = []
    for stem, _event, guid, text in US_CLIPS:
        rel = wav_relpath("US", stem)
        dest = ADDON_ROOT / rel
        write_meta(guid, rel, "WAVResourceClass")
        force = stem == "wlr"
        jobs.append(synth_clip(text, "en-US-GuyNeural", dest, force))
    for stem, _event, guid, text in USSR_CLIPS:
        rel = wav_relpath("USSR", stem)
        dest = ADDON_ROOT / rel
        write_meta(guid, rel, "WAVResourceClass")
        force = stem == "wlr"
        jobs.append(synth_clip(text, "ru-RU-DmitryNeural", dest, force))
    await asyncio.gather(*jobs)


def emit_sound_class(sound_id: int, event_name: str, bank_id: int, volume_db: int) -> str:
    return (
        f'  SoundClass "{sound_id}" {{\n'
        f"   id {sound_id}\n"
        f'   name "{event_name}"\n'
        "   version 5\n"
        f"   volume_dB {volume_db}\n"
        "   ins {\n"
        '    ConnectionsClass "con:64" {\n'
        "     id 64\n"
        "     links {\n"
        f'      ConnectionClass "{bank_id}:65" {{\n'
        f"       id {bank_id}\n"
        "       port 65\n"
        "      }\n"
        "     }\n"
        "    }\n"
        "   }\n"
        "   outState 30000\n"
        "   outStatePort 19463\n"
        "  }\n"
    )


def emit_bank(bank_id: int, name: str, guid: str, relpath: str, infinite_loop: bool) -> str:
    loop_line = ""
    if infinite_loop:
        loop_line = "   infiniteLoop 1\n   terminateLoop 1\n"
    return (
        f'  BankLocalClass "{bank_id}" {{\n'
        f"   id {bank_id}\n"
        f'   name "{name}"\n'
        "   version 7\n"
        + loop_line
        + "   Samples {\n"
        f'    AudioBankSampleClass "{name}.wav" {{\n'
        f'     Filename "{{{guid}}}{relpath}"\n'
        "    }\n"
        "   }\n"
        "  }\n"
    )


def write_acp() -> None:
    clips = [("US", US_CLIPS), ("USSR", USSR_CLIPS)]
    sound_blocks = []
    bank_blocks = []
    mixer_links = []
    sound_id = 10000
    bank_id = 20000
    for faction_dir, entries in clips:
        for stem, event_suffix, guid, _text in entries:
            event_name = "SOUND_GBRS_" + event_suffix
            rel = wav_relpath(faction_dir, stem)
            sound_blocks.append(emit_sound_class(sound_id, event_name, bank_id, 6))
            bank_blocks.append(emit_bank(bank_id, event_name, guid, rel, False))
            mixer_links.append(
                f'      ConnectionClass "{sound_id}:65" {{\n'
                f"       id {sound_id}\n"
                "       port 65\n"
                "      }"
            )
            sound_id += 1
            bank_id += 1

    hiss_rel = "Sounds/GBRS/VO/hiss.wav"
    sound_blocks.append(emit_sound_class(sound_id, HISS_EVENT, bank_id, 0))
    bank_blocks.append(emit_bank(bank_id, HISS_EVENT, HISS_GUID, hiss_rel, False))
    mixer_links.append(
        f'      ConnectionClass "{sound_id}:65" {{\n'
        f"       id {sound_id}\n"
        "       port 65\n"
        "      }"
    )

    mixer = (
        " mixers {\n"
        '  MixerClass "30000" {\n'
        "   id 30000\n"
        '   name "OutputState GBRS Intel"\n'
        "   version 4\n"
        f'   res "{FINAL_MIX}"\n'
        "   ins {\n"
        '    ConnectionsClass "con:19463" {\n'
        "     id 19463\n"
        "     links {\n"
        + "\n".join(mixer_links)
        + "\n     }\n"
        "    }\n"
        "   }\n"
        f'   path "{FINAL_MIX}"\n'
        f"   inputs {{\n    {MIXER_INPUTS}\n   }}\n"
        "  }\n"
        " }\n"
    )
    acp = (
        "AudioClass {\n"
        " sounds {\n"
        + "".join(sound_blocks)
        + " }\n"
        + mixer
        + " banks_local {\n"
        + "".join(bank_blocks)
        + " }\n"
        "}\n"
    )
    rel = "Sounds/GBRS/VO/GBRS_IntelRadio.acp"
    dest = ADDON_ROOT / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(acp, encoding="utf-8", newline="\n")
    write_meta(GUID_ACP, rel, "ACPResourceClass")


def main() -> int:
    VO_ROOT.mkdir(parents=True, exist_ok=True)
    asyncio.run(synth_all())
    for faction_dir, entries in (("US", US_CLIPS), ("USSR", USSR_CLIPS)):
        for stem, _event, _guid, _text in entries:
            strip_wav_tail(ADDON_ROOT / wav_relpath(faction_dir, stem))
    hiss_rel = "Sounds/GBRS/VO/hiss.wav"
    write_wav(ADDON_ROOT / hiss_rel, make_hiss(SAMPLE_RATE, HISS_DURATION_S), SAMPLE_RATE)
    write_meta(HISS_GUID, hiss_rel, "WAVResourceClass")
    write_acp()
    print("Wrote GBRS intel radio VO to", VO_ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
