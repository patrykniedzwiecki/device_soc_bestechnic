/*
 * Copyright (c) 2021 Bestechnic (Shanghai) Co., Ltd. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __MAIN_ENTRY_H__
#define __MAIN_ENTRY_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NOSTD
#define MAIN_ENTRY(...)                 _start(__VA_ARGS__)
#else
#if defined(NUTTX_BUILD)
#define MAIN_ENTRY(...)                 bes_main(__VA_ARGS__)
#else
#define MAIN_ENTRY(...)                 main(__VA_ARGS__)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
