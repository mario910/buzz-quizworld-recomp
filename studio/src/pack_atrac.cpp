// Native port of this repository's at3enc.py: mono ATRAC3, 48 kHz, CLC.
#include "pack_codec.h"
#include "pack_media.h"
#include <array>
#include <cmath>
#include <limits>

namespace studio::pack {
namespace {
constexpr int edges[33] = {0,   8,   16,  24,  32,  40,  48,  56,  64,  80,  96,
                           112, 128, 144, 160, 176, 192, 224, 256, 288, 320, 352,
                           384, 416, 448, 480, 512, 576, 640, 704, 768, 896, 1024};
constexpr int options[6] = {0, 1, 3, 5, 6, 7}, bitsPer[8] = {0, 2, 3, 3, 4, 4, 5, 6},
              clc[8] = {0, 4, 3, 3, 4, 4, 5, 6};
constexpr double inverseQuant[8] = {0, 1 / 1.5, 1 / 2.5, 1 / 3.5, 1 / 4.5, 1 / 7.5, 1 / 15.5, 1 / 31.5};
constexpr int maximum[8] = {0, 1, 2, 3, 4, 7, 15, 31};
struct Tables {
    std::array<double, 48> qmf;
    std::array<double, 512> window;
    std::array<double, 64> scales;
    std::array<std::array<double, 512>, 256> basis;
    Tables() {
        const double half[24] = {
            -0.00001461907,  -0.00009205479, -0.000056157569, 0.00030117269, 0.0002422519,   -0.00085293897,
            -0.0005205574,   0.0020340169,   0.00078333891,   -0.0042153862, -0.00075614988, 0.0078402944,
            -0.000061169922, -0.01344162,    0.0024626821,    0.021736089,   -0.007801671,   -0.034090221,
            0.01880949,      0.054326009,    -0.043596379,    -0.099384367,  0.13207909,     0.46424159};
        for (int i = 0; i < 24; ++i)
            qmf[i] = qmf[47 - i] = 2 * half[i];
        constexpr double pi = 3.14159265358979323846;
        std::array<double, 512> synthesis{};
        for (int i = 0; i < 128; ++i) {
            const int j = 255 - i;
            const auto wi = std::sin(((i + .5) / 256 - .5) * pi) + 1;
            const auto wj = std::sin(((j + .5) / 256 - .5) * pi) + 1;
            const auto scale = .5 * (wi * wi + wj * wj);
            synthesis[i] = synthesis[511 - i] = wi / scale;
            synthesis[j] = synthesis[511 - j] = wj / scale;
        }
        for (int n = 0; n < 512; ++n) {
            const int i = n % 256;
            window[n] =
                synthesis[n] / (synthesis[i] * synthesis[i] + synthesis[i + 256] * synthesis[i + 256]);
            for (int k = 0; k < 256; ++k)
                basis[k][n] = std::cos(pi / 256 * (n + .5 + 128) * (k + .5));
        }
        for (int i = 0; i < 64; ++i)
            scales[i] = std::pow(2., (i - 15) / 3.);
    }
};
const Tables &tables() {
    static const Tables t;
    return t;
}
struct Qmf {
    std::array<double, 46> previous{};
    void split(const double *input, int size, double *lo, double *hi) {
        std::array<double, 1070> buffer{};
        std::copy(previous.begin(), previous.end(), buffer.begin());
        std::copy(input, input + size, buffer.begin() + 46);
        const auto &t = tables();
        for (int j = 0; j < size / 2; ++j) {
            double even = 0, odd = 0;
            for (int k = 0; k < 48; k += 2) {
                even += buffer[2 * j + 47 - k] * t.qmf[k];
                odd += buffer[2 * j + 46 - k] * t.qmf[k + 1];
            }
            lo[j] = (even + odd) / 2;
            hi[j] = (even - odd) / 2;
        }
        std::copy(buffer.begin() + size, buffer.begin() + size + 46, previous.begin());
    }
};
struct Bits {
    QByteArray data = QByteArray(152, '\0');
    int position = 0;
    void add(int value, int bits) {
        if (position + bits > data.size() * 8)
            fail(QCoreApplication::translate("PackBuilder", "Przekroczony budżet ramki ATRAC3."));
        for (int i = bits - 1; i >= 0; --i, ++position)
            if ((quint32(value) >> i) & 1)
                data[position >> 3] = char(quint8(data[position >> 3]) | (1 << (7 - (position & 7))));
    }
};
QByteArray frame(const std::array<double, 1024> &spectrum) {
    const auto &t = tables();
    std::array<int, 32> scales{}, chosen{};
    double peak[32]{}, noise[32][6]{};
    for (int band = 0; band < 32; ++band) {
        double energy = 0;
        for (int k = edges[band]; k < edges[band + 1]; ++k) {
            peak[band] = std::max(peak[band], std::abs(spectrum[k]));
            energy += spectrum[k] * spectrum[k];
        }
        scales[band] = std::min<int>(63, std::lower_bound(t.scales.begin(), t.scales.end(), peak[band]) -
                                             t.scales.begin());
        noise[band][0] = energy / (edges[band + 1] - edges[band]);
        for (int o = 1; o < 6; ++o) {
            const auto step = t.scales[scales[band]] * inverseQuant[options[o]];
            noise[band][o] = step * step / 12;
        }
    }
    auto pick = [&](double threshold) {
        int last = 0;
        for (int band = 0; band < 32; ++band) {
            int o = 0;
            while (o < 5 && noise[band][o] > threshold)
                ++o;
            chosen[band] = peak[band] < 1e-9 ? 0 : options[o];
            if (chosen[band])
                last = band;
        }
        const int nb = std::min((edges[last + 1] - 1) >> 8, 3);
        int cost = 6 + 2 + 3 * (nb + 1) + 5 + 5 + 1 + 3 * (last + 1);
        if (last == 0 || last == 18 || last == 26 || last == 30)
            cost += 3;
        for (int band = 0; band <= last; ++band)
            if (chosen[band])
                cost += 6 + bitsPer[chosen[band]] * (edges[band + 1] - edges[band]);
        return cost;
    };
    double lo = -20, hi = 40;
    for (int i = 0; i < 30; ++i) {
        const auto mid = (lo + hi) / 2;
        if (pick(std::pow(2., mid)) <= 1216)
            hi = mid;
        else
            lo = mid;
    }
    pick(std::pow(2., hi));
    int last = 0;
    bool coded = false;
    for (int i = 0; i < 32; ++i)
        if (chosen[i]) {
            last = i;
            coded = true;
        }
    const int nb = coded ? std::min((edges[last + 1] - 1) >> 8, 3) : 0;
    if (coded && (last == 0 || last == 18 || last == 26 || last == 30))
        ++last;
    Bits bits;
    bits.add(0x28, 6);
    bits.add(nb, 2);
    for (int i = 0; i <= nb; ++i)
        bits.add(0, 3);
    bits.add(0, 5);
    bits.add(last, 5);
    bits.add(1, 1);
    for (int i = 0; i <= last; ++i)
        bits.add(chosen[i], 3);
    for (int i = 0; i <= last; ++i)
        if (chosen[i])
            bits.add(scales[i], 6);
    for (int i = 0; i <= last; ++i) {
        const int sel = chosen[i];
        if (!sel)
            continue;
        const auto step = t.scales[scales[i]] * inverseQuant[sel];
        auto quantize = [&](int k) {
            return std::clamp(int(std::nearbyint(spectrum[k] / step)), sel == 1 ? -2 : -maximum[sel],
                              maximum[sel]);
        };
        for (int k = edges[i]; k < edges[i + 1];) {
            if (sel == 1) {
                const int a = quantize(k++), b = quantize(k++);
                bits.add(((a & 3) << 2) | (b & 3), 4);
            } else
                bits.add(quantize(k++), clc[sel]);
        }
    }
    return bits.data;
}
QByteArray rsf(const QByteArray &frames) {
    QByteArray header = QByteArray::fromHex("05000101bb800001");
    put(header, frames.size(), 4, true);
    put(header, frames.size(), 4, true);
    put(header, 152, 4, true);
    put(header, frames.size(), 4, true);
    put(header, 0, 8, true);
    return header + frames;
}
} // namespace
QByteArray silentSpeech() {
    QByteArray f(152, '\0');
    f[0] = '\xa0';
    return rsf(f.repeated(47));
}
QByteArray encodeAtracPcm(const QByteArray &pcm, const std::atomic_bool *cancel) {
    if (pcm.isEmpty() || pcm.size() % 2 || pcm.size() > 48000 * 2 * 600)
        fail(QCoreApplication::translate("PackBuilder", "Niepoprawna długość nagrania (limit 10 minut)."));
    const int samples = pcm.size() / 2, frames = (samples + 1023) / 1024 + 2;
    Qmf full, low, high;
    std::array<std::array<double, 256>, 4> previous{};
    QByteArray result;
    result.reserve(frames * 152);
    const auto &t = tables();
    for (int f = 0; f < frames; ++f) {
        checkCancelled(cancel);
        std::array<double, 1024> input{}, spectrum{};
        for (int i = 0; i < 1024 && f * 1024 + i < samples; ++i)
            input[i] = qFromLittleEndian<qint16>(pcm.constData() + (f * 1024 + i) * 2) / 32768.;
        std::array<double, 512> lo{}, hi{};
        std::array<std::array<double, 256>, 4> bands{};
        full.split(input.data(), 1024, lo.data(), hi.data());
        low.split(lo.data(), 512, bands[0].data(), bands[1].data());
        high.split(hi.data(), 512, bands[3].data(), bands[2].data());
        for (int b = 0; b < 4; ++b) {
            std::array<double, 512> windowed{};
            for (int n = 0; n < 256; ++n) {
                windowed[n] = previous[b][n] * t.window[n];
                windowed[n + 256] = bands[b][n] * t.window[n + 256];
            }
            previous[b] = bands[b];
            for (int k = 0; k < 256; ++k) {
                double sum = 0;
                for (int n = 0; n < 512; ++n)
                    sum += windowed[n] * t.basis[k][n];
                spectrum[b * 256 + ((b & 1) ? 255 - k : k)] = -sum * 256;
            }
        }
        result += frame(spectrum);
    }
    return rsf(result);
}
} // namespace studio::pack
