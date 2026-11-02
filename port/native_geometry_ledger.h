/* Sharded diagnostic counters. Each private slot has exactly one writer;
 * atomic cells permit concurrent summaries without locked RMWs on that path.
 * Threads beyond the private inventory share an atomic overflow slot. */
#pragma once
#include <atomic>

namespace buzz {
class GeometryLedgerCounter {
    static constexpr unsigned private_slots=8;
    struct alignas(64) Slot {std::atomic<unsigned long long> value{0};};
    Slot slots_[private_slots+1];
    static unsigned index()
    {
        static std::atomic<unsigned long long> next{0};
        static thread_local unsigned mine=~0u;
        if(mine==~0u){
            const auto owner=next.fetch_add(1,std::memory_order_relaxed);
            mine=owner<private_slots?unsigned(owner):private_slots;
        }
        return mine;
    }
public:
    unsigned long long operator++(){return *this+=1;}
    unsigned long long operator+=(unsigned long long n)
    {
        const unsigned owner=index();auto& value=slots_[owner].value;
        if(owner==private_slots)return value.fetch_add(n,std::memory_order_relaxed)+n;
        const auto updated=value.load(std::memory_order_relaxed)+n;
        value.store(updated,std::memory_order_relaxed);return updated;
    }
    unsigned long long load() const
    {
        unsigned long long total=0;
        for(const auto& slot:slots_)total+=slot.value.load(std::memory_order_relaxed);
        return total;
    }
    operator unsigned long long() const{return load();}
};
}
