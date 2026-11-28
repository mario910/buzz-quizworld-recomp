"""Local OmniVoice worker. Studio passes text and file paths in a UTF-8 manifest."""
import json
import sys
import traceback
from pathlib import Path


def model_path():
    from huggingface_hub import snapshot_download
    from huggingface_hub.errors import LocalEntryNotFoundError

    repository = "k2-fsa/OmniVoice"
    # Keep the checkpoint and tokenizer license reviewed for this integration together.
    revision = "3fc979d1cd72492cd679c5ac2611e0d7ac047ba1"
    required = (
        "config.json", "model.safetensors", "tokenizer.json", "tokenizer_config.json",
        "audio_tokenizer/config.json", "audio_tokenizer/model.safetensors",
        "audio_tokenizer/preprocessor_config.json", "audio_tokenizer/LICENSE",
    )
    try:
        cached = Path(snapshot_download(repository, revision=revision, local_files_only=True))
        if all((cached / file).is_file() for file in required):
            return str(cached)
    except LocalEntryNotFoundError:
        pass
    return snapshot_download(repository, revision=revision)


def usable_gpu(torch):
    if not torch.cuda.is_available():
        return None
    for index in range(torch.cuda.device_count()):
        device = f"cuda:{index}"
        try:
            with torch.inference_mode():
                matrix = torch.ones((32, 32), device=device, dtype=torch.float16)
                if (matrix @ matrix)[0, 0].item() == 32:
                    return device
        except RuntimeError:
            traceback.print_exc()
    return None


def main():
    import soundfile
    import torch
    from omnivoice import OmniVoice

    if sys.argv[1] == "--check-device":
        import torchaudio

        installed = "rocm" if torch.version.hip else "cuda" if torch.version.cuda else "cpu"
        expected = sys.argv[2]
        if installed != expected:
            raise RuntimeError(f"Expected a {expected} PyTorch build, found {installed}")
        if expected != "cpu" and usable_gpu(torch) is None:
            raise RuntimeError(f"PyTorch {expected} could not execute an FP16 operation on any GPU")
        return

    if sys.argv[1] == "--prepare":
        import imageio_ffmpeg

        model_path()
        print("buzz-omnivoice-ffmpeg:" + imageio_ffmpeg.get_ffmpeg_exe())
        return

    manifest = Path(sys.argv[1])
    request = json.loads(manifest.read_text(encoding="utf-8"))
    device = usable_gpu(torch) or "cpu"
    dtype = torch.float16 if device.startswith("cuda:") else torch.float32
    model = OmniVoice.from_pretrained(model_path(), device_map=device, dtype=dtype)
    with torch.inference_mode():
        prompt = model.create_voice_clone_prompt(
            ref_audio=request["recording"], ref_text=request["transcript"]
        )
        for index, text in enumerate(request["texts"]):
            try:
                samples = model.generate(
                    text=text, language=request["language"], voice_clone_prompt=prompt
                )[0]
                final = manifest.parent / f"{index}.wav"
                partial = final.with_suffix(".partial.wav")
                soundfile.write(partial, samples, model.sampling_rate, subtype="PCM_16")
                partial.replace(final)
            except Exception:
                traceback.print_exc()
            print(f"buzz-omnivoice:{index}", flush=True)


if __name__ == "__main__":
    main()
