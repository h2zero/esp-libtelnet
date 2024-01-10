/*
 * Copyright [2024] [Ryan Powell]
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef __ESP_LIBTELNET_H_
#define __ESP_LIBTELNET_H_

#include <sdkconfig.h>

#ifndef TELNET_BUFFER_SIZE
    #define TELNET_BUFFER_SIZE CONFIG_ESP_LIBTELNET_BUFFER_SIZE
#endif

#ifndef TELNET_TX_CHUNK_SIZE
    #define TELNET_TX_CHUNK_SIZE CONFIG_ESP_LIBTELNET_TX_CHUNK_SIZE
#endif

#ifndef TELNET_RX_BUF_SIZE
    #define TELNET_RX_BUF_SIZE CONFIG_ESP_LIBTELNET_RX_BUF_SIZE
#endif

#ifndef TELNET_TASK_STACK_SIZE
    #define TELNET_TASK_STACK_SIZE CONFIG_ESP_LIBTELNET_TASK_STACK_SIZE
#endif

#ifndef TELNET_TASK_PRIO
    #define TELNET_TASK_PRIO CONFIG_ESP_LIBTELNET_TASK_PRIO
#endif

#ifdef CONFIG_FREERTOS_UNICORE
    #define TELNET_TASK_CORE 0
#else
    #ifndef TELNET_TASK_CORE
        #define TELNET_TASK_CORE CONFIG_ESP_LIBTELNET_TASK_CORE
    #endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef void (*telnet_rx_cb_t)(const char *buf, size_t len);

void init_telnet(telnet_rx_cb_t rx_cb);
void start_telnet(void);
void telnet_mirror_to_uart(bool enable);

#if defined(__cplusplus)
}
#endif

#endif // __ESP_LIBTELNET_H__