#include "ppu_functions.h"
#include "ppu_decode.h"
#include <QHash>
#include <QSet>
#include <algorithm>
#include <iterator>
#include <map>
#include <optional>

namespace prepare {
namespace {
using Address = quint64;
using Index = qsizetype;
using Bounds = std::map<Address, Address>;
using Range = QPair<Address, Address>;
constexpr Address AddressLimit = quint64(1) << 32;

bool inRanges(Address address, const QVector<Range> &ranges) {
    return std::any_of(ranges.cbegin(), ranges.cend(), [address](const Range &range) {
        return range.first <= address && address < range.second;
    });
}

QSet<Address> opdSeeds(const ElfImage &elf, const QVector<Range> &ranges, const Cancel &cancel) {
    Address dataLo = AddressLimit, dataHi = 0;
    QVector<QPair<Address, QByteArray>> data;
    for (const auto &segment : elf.segments()) {
        if (segment.type != 1 || (segment.flags & 1))
            continue;
        if (segment.memorySize) {
            dataLo = std::min(dataLo, segment.address);
            dataHi = std::max(dataHi, segment.address + segment.memorySize);
        }
        if (segment.fileSize)
            data.append({segment.address, elf.fileBytes(segment.offset, segment.fileSize)});
    }
    const auto scan = [&](auto visit) {
        for (const auto &[address, bytes] : data) {
            Q_UNUSED(address);
            for (qsizetype pos = 0; pos + 8 <= bytes.size(); pos += 4) {
                if (pos % 16384 == 0)
                    cancelled(cancel);
                const auto code = qFromBigEndian<quint32>(bytes.constData() + pos);
                const auto toc = qFromBigEndian<quint32>(bytes.constData() + pos + 4);
                if (code % 4 == 0 && inRanges(code, ranges))
                    visit(code, toc);
            }
        }
    };
    QHash<Address, int> frequency;
    scan([&](Address, Address toc) {
        if (toc && dataLo <= toc && toc < dataHi && !inRanges(toc, ranges))
            ++frequency[toc];
    });
    QSet<Address> seeds;
    scan([&](Address code, Address toc) {
        if (frequency.value(toc) >= 16)
            seeds.insert(code);
    });
    for (const auto &[address, bytes] : data) {
        const auto entry = elf.entry();
        if (entry >= address && entry - address + 4 <= quint64(bytes.size())) {
            const auto code = be32(bytes, entry - address);
            if (code % 4 == 0 && inRanges(code, ranges))
                seeds.insert(code);
            break;
        }
    }
    if (inRanges(elf.entry(), ranges))
        seeds.insert(elf.entry());
    return seeds;
}

bool isMflr(const PpuInstruction &i) {
    return i.mnemonic == "mflr" && i.operands == "r0";
}
bool savesLr(const PpuInstruction &i) {
    return (i.mnemonic == "stw" || i.mnemonic == "std") && i.operands.startsWith("r0,") &&
           i.operands.contains("(r1)");
}
bool allocatesStack(const PpuInstruction &i) {
    return (i.mnemonic == "stwu" || i.mnemonic == "stdu") && i.operands.startsWith("r1,") &&
           i.operands.contains("(r1)") && i.operands.contains('-');
}
bool isCfgBranch(const QString &mnemonic) {
    return mnemonic.startsWith('b') && mnemonic != "bl" && mnemonic != "blr" && mnemonic != "bctr" &&
           mnemonic != "bctrl" && mnemonic != "blrl";
}
std::optional<Address> directTarget(const PpuInstruction &i) {
    const auto operand = QStringView(i.operands).mid(i.operands.lastIndexOf(',') + 1).trimmed();
    if (!operand.startsWith(u"0x"))
        return {};
    bool ok = false;
    const auto target = operand.toUInt(&ok, 16);
    if (ok)
        return target;
    return {};
}
bool covers(const Bounds &bounds, Address address) {
    const auto next = bounds.upper_bound(address);
    return next != bounds.begin() && address < std::prev(next)->second;
}

// The SDK's leaf scan changes its stop boundaries as it visits targets.
// Preserve the integer-set slot order used by the pinned SDK reference.
// Other passes can use the native containers directly.
class LeafTargets {
  public:
    void insert(Address value) {
        const auto slot = locate(slots_, value);
        if (slots_[slot] != empty)
            return;
        slots_[slot] = value;
        ++count_;
        if (count_ * 5 < (slots_.size() - 1) * 3)
            return;
        const auto minimum = count_ * (count_ > 50000 ? 2 : 4);
        Index capacity = 8;
        while (capacity <= minimum)
            capacity *= 2;
        QVector<Address> resized(capacity, empty);
        for (const auto old : slots_) {
            if (old != empty)
                resized[locate(resized, old)] = old;
        }
        slots_.swap(resized);
    }

    const QVector<Address> &orderedSlots() const { return slots_; }
    static constexpr Address empty = AddressLimit;

  private:
    static Index locate(const QVector<Address> &table, Address value) {
        constexpr Index linearProbes = 9;
        const auto mask = table.size() - 1;
        Address perturb = value;
        Index index = value & mask;
        for (;;) {
            const Index limit = index + (index + linearProbes <= mask ? linearProbes : 0);
            for (Index probe = index; probe <= limit; ++probe) {
                if (table[probe] == empty || table[probe] == value)
                    return probe;
            }
            perturb >>= 5;
            index = (index * 5 + 1 + perturb) & mask;
        }
    }

    QVector<Address> slots_ = QVector<Address>(8, empty);
    Index count_ = 0;
};

class Finder {
  public:
    Finder(QVector<PpuInstruction> code, QSet<Address> seeds, const Cancel &cancel)
        : code_(std::move(code)), seeds_(std::move(seeds)), cancel_(cancel) {
        index_.reserve(code_.size());
        for (Index i = 0; i < code_.size(); ++i) {
            tick(i);
            const auto &insn = code_[i];
            index_.insert(insn.address, i);
            if (insn.mnemonic == "bl") {
                if (const auto target = directTarget(insn))
                    calls_.append({insn.address, *target});
            }
        }
    }

    Bounds run() {
        scanPrologues();
        scanLeaves();
        applySeeds();
        dropSlivers();
        growExtents();
        promoteCalls();
        promoteBranches();
        for (auto it = bounds_.begin(); it != bounds_.end(); ++it) {
            const auto next = std::next(it);
            const auto limit = next == bounds_.end() ? AddressLimit : next->first;
            it->second = std::min(std::max(it->second, it->first + 4), limit);
        }
        return std::move(bounds_);
    }

  private:
    void tick(Index i) const {
        if (i % 4096 == 0)
            cancelled(cancel_);
    }

    void scanPrologues() {
        for (Index i = 0; i < code_.size();) {
            tick(i);
            if (!isMflr(code_[i])) {
                ++i;
                continue;
            }
            bool saved = false;
            for (Index j = i + 1; j < std::min(i + 5, code_.size()); ++j) {
                saved |= savesLr(code_[j]);
                if (allocatesStack(code_[j]))
                    break;
            }
            if (!saved) {
                ++i;
                continue;
            }
            const Address start = code_[i > 0 && allocatesStack(code_[i - 1]) ? i - 1 : i].address;
            Address end = Address(code_[std::min(i + 100, code_.size() - 1)].address) + 4;
            QVector<Address> calls;
            for (Index j = i + 1; j < code_.size(); ++j) {
                tick(j);
                const auto &insn = code_[j];
                if (insn.mnemonic == "bl") {
                    if (const auto target = directTarget(insn))
                        calls.append(*target);
                }
                if (insn.mnemonic == "blr") {
                    end = Address(insn.address) + 4;
                    break;
                }
                if (j > i + 2 && isMflr(insn)) {
                    end = insn.address;
                    break;
                }
            }
            if (end >= start + 8) {
                bounds_[start] = end;
                if (!prologueCalls_.contains(start))
                    prologueOrder_.append(start);
                prologueCalls_.insert(start, std::move(calls));
            }
            i = std::max(i + 1, index_.value(end, i + 1));
        }
    }

    Address scanReturn(Address start, Index window, Address cap = AddressLimit) const {
        const Index index = index_.value(start, -1);
        if (index < 0)
            return start;
        for (Index j = index; j < std::min(index + window, code_.size()); ++j) {
            const auto &insn = code_[j];
            if (insn.address >= cap)
                return cap;
            if (insn.mnemonic == "blr")
                return Address(insn.address) + 4;
            if (j > index && bounds_.count(insn.address))
                return insn.address;
        }
        return start;
    }

    void scanLeaves() {
        LeafTargets targets;
        for (const auto start : prologueOrder_) {
            const auto found = prologueCalls_.constFind(start);
            for (const auto target : found.value())
                targets.insert(target);
        }
        prologueCalls_.clear();
        prologueOrder_.clear();
        Address lo = AddressLimit, hi = 0;
        if (!seeds_.isEmpty()) {
            const auto [first, last] = std::minmax_element(seeds_.cbegin(), seeds_.cend());
            lo = *first;
            hi = *last;
        }
        for (const auto &[source, target] : calls_) {
            if ((lo <= source && source <= hi) || covers(bounds_, source))
                targets.insert(target);
        }
        for (const auto target : targets.orderedSlots()) {
            if (target == LeafTargets::empty || bounds_.count(target))
                continue;
            const auto end = scanReturn(target, 200);
            if (end >= target + 8)
                bounds_[target] = end;
        }
    }

    void applySeeds() {
        QVector<Address> added;
        for (const auto seed : seeds_) {
            if (index_.contains(seed) && bounds_.emplace(seed, seed + 4).second)
                added.append(seed);
        }
        for (const auto seed : added) {
            const auto next = bounds_.upper_bound(seed);
            const auto cap = next == bounds_.end() ? AddressLimit : next->first;
            const auto end = scanReturn(seed, 2000, cap);
            bounds_[seed] = end > seed ? end : (cap == AddressLimit ? seed + 4 : cap);
        }
    }

    void dropSlivers() {
        for (const auto &[source, target] : calls_) {
            Q_UNUSED(source);
            callTargets_.insert(target);
        }
        QVector<Address> ordered(seeds_.cbegin(), seeds_.cend());
        std::sort(ordered.begin(), ordered.end());
        for (auto it = bounds_.begin(); it != bounds_.end();) {
            const auto start = it->first;
            const auto next = std::upper_bound(ordered.cbegin(), ordered.cend(), start);
            if (next == ordered.cbegin() || seeds_.contains(start) || callTargets_.contains(start)) {
                ++it;
                continue;
            }
            const auto seed = *std::prev(next);
            const auto first = index_.value(seed, -1), last = index_.value(start, -1);
            if (start - seed > 32 || first < 0 || last < first ||
                std::any_of(
                    code_.cbegin() + first, code_.cbegin() + last,
                    [](const PpuInstruction &i) {
                        return i.mnemonic == "blr" || i.mnemonic == "b" || i.mnemonic == "rfid";
                    })) {
                ++it;
                continue;
            }
            it = bounds_.erase(it);
        }
    }

    void growExtents() {
        for (auto it = bounds_.begin(); it != bounds_.end(); ++it) {
            cancelled(cancel_);
            const auto start = it->first;
            const auto next = std::next(it);
            const auto cap = next == bounds_.end() ? Address(code_.last().address) + 4 : next->first;
            auto end = it->second;
            QSet<Address> seen;
            QVector<Address> pending{start};
            while (!pending.isEmpty()) {
                const auto block = pending.takeLast();
                if (seen.contains(block))
                    continue;
                seen.insert(block);
                const auto index = index_.value(block, -1);
                if (index < 0)
                    continue;
                for (Index j = index; j < code_.size() && code_[j].address < cap; ++j) {
                    tick(j);
                    const auto &insn = code_[j];
                    end = std::max(end, Address(insn.address) + 4);
                    if (isCfgBranch(insn.mnemonic)) {
                        const auto target = directTarget(insn);
                        if (target && start <= *target && *target < cap)
                            pending.append(*target);
                    }
                    if (insn.mnemonic == "blr" || insn.mnemonic == "b" || insn.mnemonic == "ba" ||
                        insn.mnemonic == "bctr")
                        break;
                }
            }
            it->second = std::max(it->second, std::min(end, cap));
        }
    }

    void promoteCalls() {
        QSet<Address> targets;
        for (const auto &[source, target] : calls_) {
            if (covers(bounds_, source) && !covers(bounds_, target) && !bounds_.count(target))
                targets.insert(target);
        }
        QVector<Address> ordered(targets.cbegin(), targets.cend());
        std::sort(ordered.begin(), ordered.end());
        for (const auto start : ordered) {
            const auto index = index_.value(start, -1);
            if (index < 0)
                continue;
            Address end = Address(code_.last().address) + 4;
            for (Index j = index; j < code_.size(); ++j) {
                tick(j);
                const auto &insn = code_[j];
                if (j > index && bounds_.count(insn.address)) {
                    end = insn.address;
                    break;
                }
                const auto &mn = insn.mnemonic;
                if (mn == "blr" || mn == "bctr" || mn == "b" || mn == "blrl" || mn == "rfid") {
                    end = Address(insn.address) + 4;
                    break;
                }
            }
            if (end > start)
                bounds_[start] = end;
        }
    }

    bool hasPrologue(Address start) const {
        const auto index = index_.value(start, -1);
        if (index < 0)
            return false;
        if (isMflr(code_[index]) || allocatesStack(code_[index]))
            return true;
        for (Index j = index + 1; j < std::min(index + 3, code_.size()); ++j) {
            if (isMflr(code_[j]))
                return true;
        }
        return false;
    }

    void promoteBranches() {
        QSet<Address> targets;
        for (Index i = 0; i < code_.size(); ++i) {
            tick(i);
            const auto &insn = code_[i];
            if (!isCfgBranch(insn.mnemonic) || !covers(bounds_, insn.address))
                continue;
            const auto target = directTarget(insn);
            if (target && !covers(bounds_, *target) && !bounds_.count(*target))
                targets.insert(*target);
        }
        QVector<Address> ordered(targets.cbegin(), targets.cend());
        std::sort(ordered.begin(), ordered.end());
        for (const auto start : ordered) {
            if (!callTargets_.contains(start) && !seeds_.contains(start) && !hasPrologue(start))
                continue;
            const auto end = scanReturn(start, 500);
            if (end >= start + 4)
                bounds_[start] = end;
        }
    }

    QVector<PpuInstruction> code_;
    QSet<Address> seeds_;
    Cancel cancel_;
    QHash<Address, Index> index_;
    QVector<QPair<Address, Address>> calls_;
    QVector<Address> prologueOrder_;
    QHash<Address, QVector<Address>> prologueCalls_;
    QSet<Address> callTargets_;
    Bounds bounds_;
};
} // namespace

QJsonArray findPpuFunctions(const ElfImage &elf, const QJsonArray &externalSeeds, const Cancel &cancel) {
    require(elf.is64() && elf.machine() == 21, "Expected a PPU ELF64 executable.");
    QVector<PpuInstruction> code;
    QVector<Range> ranges;
    for (const auto &segment : elf.segments()) {
        if (segment.type != 1 || !(segment.flags & 1) || !segment.fileSize)
            continue;
        require(segment.address < AddressLimit && segment.fileSize <= AddressLimit - segment.address,
                "Invalid PPU executable range.");
        ranges.append({segment.address, segment.address + segment.fileSize});
        const auto bytes = elf.fileBytes(segment.offset, segment.fileSize);
        code.reserve(code.size() + bytes.size() / 4);
        for (qsizetype offset = 0; offset + 4 <= bytes.size(); offset += 4) {
            if (offset % 16384 == 0)
                cancelled(cancel);
            code.append(
                decodePpu(qFromBigEndian<quint32>(bytes.constData() + offset), segment.address + offset));
        }
    }
    require(!code.isEmpty(), "No executable PPU instructions.");
    auto seeds = opdSeeds(elf, ranges, cancel);
    for (const auto &entry : externalSeeds) {
        const auto value = entry.toObject().value("start");
        bool ok = false;
        const auto start = value.toString().toULongLong(&ok, 0);
        require(ok, "Invalid PPU function seed.");
        if (start % 4 == 0 && inRanges(start, ranges))
            seeds.insert(start);
    }
    QJsonArray functions;
    const auto hex = [](Address address) {
        return "0x" + QString::number(address, 16).toUpper().rightJustified(8, '0');
    };
    for (const auto &[start, end] : Finder(std::move(code), std::move(seeds), cancel).run())
        functions.append(QJsonObject{{"start", hex(start)}, {"end", hex(end)}});
    return functions;
}
} // namespace prepare
