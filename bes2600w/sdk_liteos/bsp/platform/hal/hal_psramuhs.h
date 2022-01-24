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
#ifndef __HAL_PSRAMUHS_H__
#define __HAL_PSRAMUHS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "plat_types.h"

void hal_psramuhs_sleep(void);
void hal_psramuhs_wakeup(void);
void hal_psramuhs_init(void);
void hal_psramuhs_hold();
void hal_psramuhs_release();
void psramuhsphy_sleep(void);
void hal_psramuhs_refresh_enable();
void hal_psramuhs_refresh_disable();
uint32_t hal_psramuhs_ca_calib_result();
#ifdef __cplusplus
}
#endif

#endif

