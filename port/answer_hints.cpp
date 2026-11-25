#include "answer_hints.h"
#include "draw_observer.h"
#include <chrono>
#include <mutex>

namespace buzz_hints {
namespace {
struct Seen {Box box{};uint64_t at=0;};
std::mutex seen_mutex;
std::array<Seen,board_count> seen{};
uint64_t now_ms(){
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
// RSX thread: cheap rejection on the fragment constants before any vertex work.
void observe(const buzz_draw_observation* draw){
    const int board=classify(draw->fs_constants,draw->fs_constant_count);
    if(board<0)return;
    Box box{};
    if(!project(draw->vs_constants,draw->vs_constant_count,draw->vertices,draw->vertex_count,
                draw->vertex_stride,draw->position_offset,draw->target_width,draw->target_height,box))return;
    if(!is_answer_board(box))return; // a score panel's answer marker, not a board
    std::lock_guard lock(seen_mutex);
    seen[size_t(board)]={box,now_ms()};
}
}
void install(){buzz_draw_observer=observe;}
std::array<std::optional<Box>,board_count> visible_boards(uint64_t max_age_ms){
    std::array<std::optional<Box>,board_count> result{};
    const uint64_t now=now_ms();
    std::lock_guard lock(seen_mutex);
    for(size_t i=0;i<seen.size();++i)if(seen[i].at&&now-seen[i].at<=max_age_ms)result[i]=seen[i].box;
    return result;
}
}
