"""Minimal ATRAC3 encoder (mono, 48 kHz, 152-byte frames) for Buzz! speech.

Bitstream and synthesis follow FFmpeg's atrac3 decoder (libavcodec/atrac3.c,
atrac.c), which is also used to verify the output. Features: 4-band QMF,
256-point MDCT per band, constant-length (CLC) mantissa coding, per-frame
bit allocation. No gain control or tonal components (allowed by the format).

  python at3enc.py <in.wav|any FFmpeg input> <out.rsf> [--gain dB]

Needs numpy.
"""
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

SAMPLES = 1024
FRAME_BYTES = 0x98
SUBBAND_TAB = np.array([0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176,
                        192, 224, 256, 288, 320, 352, 384, 416, 448, 480, 512, 576, 640, 704,
                        768, 896, 1024])
INV_MAX_QUANT = np.array([0.0, 1 / 1.5, 1 / 2.5, 1 / 3.5, 1 / 4.5, 1 / 7.5, 1 / 15.5, 1 / 31.5])
CLC_BITS = [0, 4, 3, 3, 4, 4, 5, 6]          # selector 1: 4 bits per pair of coefficients
SF_TABLE = 2.0 ** ((np.arange(64) - 15) / 3.0)
OPTIONS = [0, 1, 3, 5, 6, 7]                  # selectors worth using (2 and 4 are dominated)
BITS_PER_COEF = {0: 0, 1: 2, 3: 3, 5: 4, 6: 5, 7: 6}
MAXQ = {1: 1, 3: 3, 5: 7, 6: 15, 7: 31}       # selector 1 also allows -2

_QMF_HALF = [-0.00001461907, -0.00009205479, -0.000056157569, 0.00030117269,
             0.0002422519, -0.00085293897, -0.0005205574, 0.0020340169,
             0.00078333891, -0.0042153862, -0.00075614988, 0.0078402944,
             -0.000061169922, -0.01344162, 0.0024626821, 0.021736089,
             -0.007801671, -0.034090221, 0.01880949, 0.054326009,
             -0.043596379, -0.099384367, 0.13207909, 0.46424159]
QMF = np.array([2 * x for x in _QMF_HALF] + [2 * x for x in reversed(_QMF_HALF)])


def _mdct_window():
    w = np.zeros(512)
    for i, j in zip(range(128), range(255, 127, -1)):
        wi = np.sin(((i + 0.5) / 256.0 - 0.5) * np.pi) + 1.0
        wj = np.sin(((j + 0.5) / 256.0 - 0.5) * np.pi) + 1.0
        s = 0.5 * (wi * wi + wj * wj)
        w[i] = w[511 - i] = wi / s
        w[j] = w[511 - j] = wj / s
    return w


SYN_WIN = _mdct_window()
_n = np.arange(512)[:, None]
_k = np.arange(256)[None, :]
MDCT_BASIS = np.cos(np.pi / 256 * (_n + 0.5 + 128) * (_k + 0.5))   # 512 x 256
IMDCT_SCALE = 1.0 / 32768
# Analysis window chosen so analysis*synthesis satisfies TDAC (see _selftest).
ANA_WIN = np.zeros(512)


def _set_analysis_window():
    # TDAC with different windows: a[n]*s[n] + a[n+256]*s[n+256] must be constant and
    # the time-reversed pairs must cancel. With the symmetric decoder window s, the
    # analysis window a[n] = s[n] / (s[n]^2 + s[n+256]^2) (n < 256), mirrored, works.
    s = SYN_WIN
    a = np.zeros(512)
    d = s[:256] ** 2 + s[256:] ** 2
    a[:256] = s[:256] / d
    a[256:] = s[256:] / d
    ANA_WIN[:] = a


_set_analysis_window()


# ---------------------------------------------------------------- decoder model
def iqmf(lo, hi, delay):
    """FFmpeg ff_atrac_iqmf for whole signals (delay = previous 46 samples)."""
    n = len(lo)
    p3 = np.empty(2 * n)
    p3[0::2] = lo + hi
    p3[1::2] = lo - hi
    buf = np.concatenate([delay, p3])
    out = np.empty(2 * n)
    # pOut[2j] = sum odd taps of buf[2j:2j+48], pOut[2j+1] = sum even taps
    idx = 2 * np.arange(n)[:, None] + np.arange(48)[None, :]
    win = buf[idx]
    out[0::2] = (win[:, 1::2] * QMF[1::2]).sum(1)
    out[1::2] = (win[:, 0::2] * QMF[0::2]).sum(1)
    return out, buf[-46:]


def decode_model(spec):
    """spec: frames x 1024 coefficients -> PCM float (same scale as FFmpeg s16/32768,
    same polarity as FFmpeg, whose IMDCT is sign-inverted vs the textbook basis)."""
    spec = -np.asarray(spec)
    f = spec.shape[0]
    bands = []
    for b in range(4):
        x = spec[:, b * 256:(b + 1) * 256].copy()
        if b & 1:
            x = x[:, ::-1]
        y = (x @ MDCT_BASIS.T) * IMDCT_SCALE * SYN_WIN          # frames x 512
        out = np.zeros((f, 256))
        prev = np.zeros(256)
        for i in range(f):
            out[i] = y[i, :256] + prev
            prev = y[i, 256:]
        bands.append(out.reshape(-1))
    z = np.zeros(46)
    low, _ = iqmf(bands[0], bands[1], z)
    high, _ = iqmf(bands[3], bands[2], z)
    full, _ = iqmf(low, high, z)
    return full


# ---------------------------------------------------------------- encoder
def qmf_analysis(x):
    """Inverse of iqmf: split x (len 2n) into (lo, hi) of length n each."""
    n = len(x) // 2
    buf = np.concatenate([np.zeros(46), x])
    idx = 2 * np.arange(n)[:, None] + np.arange(48)[None, :]
    win = buf[idx][:, ::-1]                    # reversed window position (time-reversed filter)
    even = (win[:, 0::2] * QMF[0::2]).sum(1)
    odd = (win[:, 1::2] * QMF[1::2]).sum(1)
    return (even + odd) / 2, (even - odd) / 2     # iqmf has a gain of 2


def analysis(pcm):
    """PCM float -> frames x 1024 coefficients."""
    n = -(-len(pcm) // SAMPLES) * SAMPLES + 2 * SAMPLES       # flush the filter delays
    x = np.zeros(n)
    x[:len(pcm)] = pcm
    low, high = qmf_analysis(x)
    b0, b1 = qmf_analysis(low)
    b3, b2 = qmf_analysis(high)
    frames = n // SAMPLES
    spec = np.zeros((frames, 1024))
    for b, sig in enumerate((b0, b1, b2, b3)):
        padded = np.concatenate([np.zeros(256), sig])          # frame i uses samples [256(i-1), 256(i+1))
        blocks = np.lib.stride_tricks.sliding_window_view(padded, 512)[::256][:frames]
        # FFmpeg's (and Sony's) IMDCT output is sign-inverted relative to
        # decode_model's textbook basis: negate so decoded polarity matches the input.
        c = -(blocks * ANA_WIN) @ MDCT_BASIS / (IMDCT_SCALE * 128)
        if b & 1:
            c = c[:, ::-1]
        spec[:, b * 256:(b + 1) * 256] = c
    return spec


def _quantise(c, sel, sf):
    """Mantissas for one subband."""
    q = c / (SF_TABLE[sf] * INV_MAX_QUANT[sel])
    m = np.rint(q).astype(int)
    if sel == 1:
        return np.clip(m, -2, 1)
    return np.clip(m, -MAXQ[sel], MAXQ[sel])


def allocate(spec, budget_bits=FRAME_BYTES * 8):
    """Choose selector + scale factor per subband for every frame.
    Vectorised: binary search a per-frame noise threshold so the frame fits."""
    f = spec.shape[0]
    size = np.diff(SUBBAND_TAB)                                 # 32
    seg = [spec[:, a:b] for a, b in zip(SUBBAND_TAB[:-1], SUBBAND_TAB[1:])]
    peak = np.stack([np.abs(s).max(1) for s in seg], 1)        # f x 32
    sf = np.clip(np.searchsorted(SF_TABLE, peak, side="left"), 0, 63)
    # noise power per option ~ (step^2)/12 per coefficient, step = sf*inv_max_quant
    steps = np.stack([SF_TABLE[sf] * INV_MAX_QUANT[o] if o else np.full(sf.shape, np.inf)
                      for o in OPTIONS], 2)                     # f x 32 x opt
    energy = np.stack([(s ** 2).mean(1) for s in seg], 1)
    noise = np.where(np.isinf(steps), energy[..., None], steps ** 2 / 12)
    cost = np.array([BITS_PER_COEF[o] for o in OPTIONS])[None, None, :] * size[None, :, None] \
        + np.array([0] + [6] * (len(OPTIONS) - 1))[None, None, :]

    def pick(th):
        ok = noise <= th[:, None, None]
        # cheapest option meeting threshold, else the finest option
        choice = np.where(ok.any(2), ok.argmax(2), len(OPTIONS) - 1)
        choice = np.where(peak < 1e-9, 0, choice)
        bits = np.take_along_axis(cost, choice[..., None], 2)[..., 0]
        coded = choice > 0
        last = np.where(coded.any(1), 31 - coded[:, ::-1].argmax(1), 0)
        nb = np.minimum((SUBBAND_TAB[last + 1] - 1) >> 8, 3)
        mask = np.arange(32)[None, :] <= last[:, None]
        total = 6 + 2 + 3 * (nb + 1) + 5 + 5 + 1 + 3 * (last + 1) + (bits * mask).sum(1) \
            + 3 * np.isin(last, (0, 18, 26, 30))     # see pack_frame
        return choice, total

    lo = np.full(f, -20.0)
    hi = np.full(f, 40.0)                                       # log2 threshold
    for _ in range(30):
        mid = (lo + hi) / 2
        _, total = pick(2.0 ** mid)
        fits = total <= budget_bits
        hi = np.where(fits, mid, hi)
        lo = np.where(fits, lo, mid)
    choice, total = pick(2.0 ** hi)
    sel = np.array(OPTIONS)[choice]
    return sel, sf


class _Bits:
    def __init__(self):
        self.v, self.n = 0, 0

    def put(self, value, bits):
        self.v = (self.v << bits) | (value & ((1 << bits) - 1))
        self.n += bits

    def bytes(self, size):
        if self.n > size * 8:
            raise ValueError("frame overflow")
        return (self.v << (size * 8 - self.n)).to_bytes(size, "big")


CLC_PAIR = {0: 0, 1: 1, -2: 2, -1: 3}


def pack_frame(coefs, sel, sf):
    coded = np.nonzero(sel)[0]
    last = int(coded[-1]) if len(coded) else 0
    nb = min((int(SUBBAND_TAB[last + 1]) - 1) >> 8, 3) if len(coded) else 0
    # FFmpeg derives the synthesised band count from the START of the last
    # listed subband; when that subband starts a QMF band (0, 18, 26, 30) list
    # one more, uncoded, subband so every decoder synthesises the band.
    if len(coded) and last in (0, 18, 26, 30):
        last += 1
    w = _Bits()
    w.put(0x28, 6)
    w.put(nb, 2)
    for _ in range(nb + 1):
        w.put(0, 3)            # no gain points
    w.put(0, 5)                # no tonal components
    w.put(last, 5)
    w.put(1, 1)                # CLC
    for i in range(last + 1):
        w.put(int(sel[i]), 3)
    for i in range(last + 1):
        if sel[i]:
            w.put(int(sf[i]), 6)
    for i in range(last + 1):
        s = int(sel[i])
        if not s:
            continue
        m = _quantise(coefs[SUBBAND_TAB[i]:SUBBAND_TAB[i + 1]], s, int(sf[i]))
        if s == 1:
            for a, b in zip(m[0::2], m[1::2]):
                w.put((CLC_PAIR[int(a)] << 2) | CLC_PAIR[int(b)], 4)
        else:
            for v in m:
                w.put(int(v), CLC_BITS[s])
    return w.bytes(FRAME_BYTES)


def encode(pcm):
    """PCM float in [-1, 1) at 48 kHz mono -> concatenated ATRAC3 frames."""
    spec = analysis(np.asarray(pcm, dtype=np.float64))
    sel, sf = allocate(spec)
    return b"".join(pack_frame(spec[i], sel[i], sf[i]) for i in range(spec.shape[0]))


def read_audio(path, gain_db=0.0):
    pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar", "48000",
                          "-af", f"volume={gain_db}dB", "-f", "s16le", "-"],
                         check=True, capture_output=True).stdout
    return np.frombuffer(pcm, dtype="<i2").astype(np.float64) / 32768.0


def encode_file(path, gain_db=0.0):
    """Any FFmpeg-readable audio, or a Wwise ATRAC9 .wem, -> .rsf bytes."""
    import tempfile
    path = Path(path)
    if path.suffix.lower() == ".wem":
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from kip_export import wem_to_at9
        with tempfile.TemporaryDirectory() as td:
            at9 = Path(td) / "voice.at9"
            at9.write_bytes(wem_to_at9(path.read_bytes()))
            pcm = read_audio(at9, gain_db)
    else:
        pcm = read_audio(path, gain_db)
    return to_rsf(encode(pcm))


def to_rsf(frames):
    size = len(frames)
    return struct.pack(">BBBBHHIIIIQ", 5, 0, 1, 1, 48000, 1, size, size, FRAME_BYTES, size, 0) + frames


def _selftest():
    rng = np.random.default_rng(1)
    t = np.arange(48000 * 2) / 48000
    x = 0.3 * np.sin(2 * np.pi * 440 * t) + 0.05 * rng.standard_normal(len(t))
    y = decode_model(analysis(x))
    best = max(range(0, 4096), key=lambda d: np.dot(x[:40000], y[d:d + 40000]))
    err = y[best:best + len(x) - 4096] - x[:len(x) - 4096]
    snr = 10 * np.log10((x ** 2).mean() / (err ** 2).mean())
    print(f"transform round trip: delay {best} samples, SNR {snr:.1f} dB")


if __name__ == "__main__":
    if sys.argv[1:2] == ["selftest"]:
        _selftest()
    elif len(sys.argv) >= 3:
        gain = float(sys.argv[sys.argv.index("--gain") + 1]) if "--gain" in sys.argv else 0.0
        Path(sys.argv[2]).write_bytes(to_rsf(encode(read_audio(sys.argv[1], gain))))
    else:
        print(__doc__)
