/*
 * Copyright (C) 2020 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Reference: https://www.linkedin.com/pulse/using-simd-arm-neon-speed-up-applications-salvatore-c-chieppa */

#include <nuttx/config.h>

#if defined(CONFIG_LV_FBDEV_ROTATE_90) || defined(CONFIG_LV_FBDEV_ROTATE_270)

 .title "rotate_neon"

.text

.global rotate_neon
.func rotate_neon

DST     .req r0
SRC     .req r1
STRIDE  .req r2
WH      .req r3
H       .req r4
X       .req r5
Y       .req r6
TMP     .req r8

rotate_neon:
    push        {r4-r6, r8, lr}
    mov         H, WH, lsr #16
    cmp         H, #4
    blt         skip
    movt        WH, #0
    cmp         WH, #4
    blt         skip
    mov         WH, WH, lsl #2           @WH = W * 4 (source stride)
    mov         Y, #0
    push        {DST, SRC}
outer_loop:
    ldm         sp, {DST, SRC}
    mov         X, #0
    mla         SRC, Y, WH, SRC
#if defined(CONFIG_LV_FBDEV_ROTATE_90)
    add         Y, #4
    add         DST, DST, H, lsl #2
    sub         DST, DST, Y, lsl #2
#else
    mov         TMP, WH, lsr #2
    mla         DST, TMP, STRIDE, DST
    add         DST, DST, Y, lsl #2
    add         Y, #4
#endif
load4x4:
    add         TMP, SRC, X, lsl #2
    pld         [TMP, #64]
    vld1.32     {q0}, [TMP], WH
    pld         [TMP, #64]
    vld1.32     {q1}, [TMP], WH
    pld         [TMP, #64]
    vld1.32     {q2}, [TMP], WH
    pld         [TMP, #64]
    vld1.32     {q3}, [TMP], WH
#if defined(CONFIG_LV_FBDEV_ROTATE_90)
    vswp        d0, d5
    vswp        d2, d7
    vtrn.32     q0, q1
    vtrn.32     q2, q3
    vrev64.32   q0, q0
    vrev64.32   q1, q1
    vrev64.32   q2, q2
    vrev64.32   q3, q3
    vswp        q0, q2
    vswp        q1, q3
    mla         TMP, X, STRIDE, DST
    add         X, #4
#else
    vtrn.32     q0, q1
    vtrn.32     q2, q3
    vswp        d1, d4
    vswp        d3, d6
    add         X, #4
    vswp        q0, q3
    vswp        q1, q2
    mls         TMP, X, STRIDE, DST
#endif
    vst1.32     {q0}, [TMP], STRIDE
    vst1.32     {q1}, [TMP], STRIDE
    vst1.32     {q2}, [TMP], STRIDE
    vst1.32     {q3}, [TMP], STRIDE
    rsbs        TMP, X, WH, lsr #2
    beq         inner_loop_end
    cmp         TMP, #3                  @overlap tail
    itt         le
    movle       X, WH, lsr #2
    suble       X, #4
    b           load4x4
inner_loop_end:
    subs        TMP, H, Y
    beq         outer_loop_end
    cmp         TMP, #3
    bgt         outer_loop
    ldm         sp, {DST, SRC}
    mov         X, #0
    sub         Y, H, #4
    mla         SRC, Y, WH, SRC
#if defined(CONFIG_LV_FBDEV_ROTATE_270)
    mov         TMP, WH, lsr #2
    mla         DST, TMP, STRIDE, DST
    add         DST, DST, Y, lsl #2
#endif
    add         Y, #4
    b           load4x4
outer_loop_end:
    pop         {DST, SRC}
skip:
    pop         {r4-r6, r8, pc}

.endfunc

#endif /* CONFIG_LV_FBDEV_ROTATE_90 || CONFIG_LV_FBDEV_ROTATE_270 */
