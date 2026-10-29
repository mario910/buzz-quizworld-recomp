#ifndef BUZZ_NATIVE_IMAGE36_H
#define BUZZ_NATIVE_IMAGE36_H
#include "spu_context.h"
#ifdef __cplusplus
extern "C" {
#endif
extern int buzz_image36_mode;
void buzz_image36_run(spu_context*, void (*reference)(spu_context*), void (*native)(spu_context*));
unsigned buzz_image36_mismatches(void);
void buzz_image36_report(void);
#ifdef __cplusplus
}
#endif
#endif
