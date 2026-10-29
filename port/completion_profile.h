#pragma once
// Only the hash-checked generated adapter and its tests select this profile.
#ifdef BUZZ_COMPLETION_US100
#define BUZZ_COMPLETION_FN func_002689D0
#define BUZZ_COMPLETION_POLL_FN func_006A3444
#define BUZZ_COMPLETION_RETIRE_FN func_002688F0
constexpr uint32_t buzz_completion_busy_lr1 = 0x26A1D4, buzz_completion_busy_lr2 = 0x26A368;
constexpr uint32_t buzz_completion_poll_lr = 0x268A00, buzz_completion_retire_lr = 0x268A18;
#elif defined(BUZZ_COMPLETION_DISC100)
#define BUZZ_COMPLETION_FN func_00268108
#define BUZZ_COMPLETION_POLL_FN func_006A4214
#define BUZZ_COMPLETION_RETIRE_FN func_00268028
constexpr uint32_t buzz_completion_busy_lr1 = 0x26990C, buzz_completion_busy_lr2 = 0x269AA0;
constexpr uint32_t buzz_completion_poll_lr = 0x268138, buzz_completion_retire_lr = 0x268150;
#else
#define BUZZ_COMPLETION_FN func_0026A8A0
#define BUZZ_COMPLETION_POLL_FN func_0068CF6C
#define BUZZ_COMPLETION_RETIRE_FN func_0026A7C4
constexpr uint32_t buzz_completion_busy_lr1 = 0x26BE10, buzz_completion_busy_lr2 = 0x26BF94;
constexpr uint32_t buzz_completion_poll_lr = 0x26A8D0, buzz_completion_retire_lr = 0x26A8E8;
#endif
