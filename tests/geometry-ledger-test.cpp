#include "../port/native_geometry_ledger.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
using buzz::GeometryLedgerCounter;

static void run(GeometryLedgerCounter& a,GeometryLedgerCounter& b,
                unsigned threads,unsigned iterations,bool private_returns)
{
    const auto before_a=a.load(),before_b=b.load();
    unsigned long long add_a=0,add_b=0;
    std::atomic<unsigned> ready{0},done{0};std::atomic<bool> go{false};
    std::vector<std::thread> workers;
    for(unsigned id=0;id<threads;++id){
        const unsigned amount=id+1;add_a+=iterations*amount;add_b+=iterations*(amount+3ull);
        workers.emplace_back([&,amount]{
            ready.fetch_add(1,std::memory_order_release);
            while(!go.load(std::memory_order_acquire))std::this_thread::yield();
            for(unsigned i=0;i<iterations;++i){
                const auto va=(a+=amount),vb=(b+=amount+3);
                if(private_returns){assert(va==(i+1ull)*amount);assert(vb==(i+1ull)*(amount+3));}
            }
            done.fetch_add(1,std::memory_order_release);
        });
    }
    while(ready.load(std::memory_order_acquire)!=threads)std::this_thread::yield();
    go.store(true,std::memory_order_release);
    unsigned long long prior_a=before_a,prior_b=before_b;
    while(done.load(std::memory_order_acquire)!=threads){
        const auto va=a.load(),vb=b.load();
        assert(va>=prior_a && va<=before_a+add_a && vb>=prior_b && vb<=before_b+add_b);
        prior_a=va;prior_b=vb;std::this_thread::yield();
    }
    for(auto& worker:workers)worker.join();
    assert(a.load()==before_a+add_a && b.load()==before_b+add_b);
    assert(static_cast<unsigned long long>(a)==a.load());
}

int main()
{
    GeometryLedgerCounter a,b;
    assert(!a.load() && !b.load());assert(++a==1 && (a+=16)==17 && (b+=33)==33);
    // Main owns slot0. Seven simultaneously active writers each own one of
    // the remaining private slots; their returned local totals must be exact.
    run(a,b,7,200000,true);
    // Private owners have now exited. New threads must never reuse their
    // slots:32 concurrent writers exercise the shared overflow counter.
    run(a,b,32,50000,false);
    run(a,b,12,50000,false);
    assert(++a==18 && (b+=4)==37); // main's private counts survive the churn
    std::printf("PASS ledger: two counters,7200000 concurrent updates,private return values,concurrent monotonic summaries,32 overflow writers and thread churn;totals %llu/%llu\n",a.load(),b.load());
}
