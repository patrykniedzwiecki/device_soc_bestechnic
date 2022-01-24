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
#ifndef __HAL_BTPCM_H__
#define __HAL_BTPCM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "plat_types.h"
#include "hal_aud.h"

enum HAL_BTPCM_ID_T {
	HAL_BTPCM_ID_0 = 0,
	HAL_BTPCM_ID_NUM,
};

struct HAL_BTPCM_CONFIG_T {
    uint32_t bits;
    uint32_t channel_num;
    uint32_t sample_rate;

    uint32_t use_dma;
};

int hal_btpcm_open(enum HAL_BTPCM_ID_T id, enum AUD_STREAM_T stream);
int hal_btpcm_close(enum HAL_BTPCM_ID_T id, enum AUD_STREAM_T stream);
int hal_btpcm_start_stream(enum HAL_BTPCM_ID_T id, enum AUD_STREAM_T stream);
int hal_btpcm_stop_stream(enum HAL_BTPCM_ID_T id, enum AUD_STREAM_T stream);
int hal_btpcm_setup_stream(enum HAL_BTPCM_ID_T id, enum AUD_STREAM_T stream, struct HAL_BTPCM_CONFIG_T *cfg);
int hal_btpcm_send(enum HAL_BTPCM_ID_T id, const uint8_t *value, uint32_t value_len);
uint8_t hal_btpcm_recv(enum HAL_BTPCM_ID_T id, uint8_t *value, uint32_t value_len);

#ifdef __cplusplus
}
#endif

#endif
