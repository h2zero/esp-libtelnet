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

#include <esp_log.h>
#include <freertos/ringbuf.h>
#include <lwip/sockets.h>
#include <libtelnet.h>
#include "esp-libtelnet.h"

extern void ets_install_putc1(void (*p)(char c));

struct telnetClientData {
    int conn_fd;
    telnet_t *tnHandle;
    char rxbuf[TELNET_RX_BUF_SIZE];
};

static const char*     tag = "telnet";
static int             uart_fd = -1;
static RingbufHandle_t telnet_buf_handle;
static bool            mirror_to_uart = true;
static TaskHandle_t    telnet_task_handle = NULL;
static telnet_rx_cb_t  telnet_rx_cb = NULL;

static void            telnet_task(void *data);
ssize_t                telnet_write(void *cookie, const char *data, ssize_t size);
void                   telnet_putc(char c);
static void            telnet_handle_conn(int conn_fd);

static void telnet_event_handler(telnet_t *thisTelnet, telnet_event_t *event, void *userData) {
    struct telnetClientData *client = (struct telnetClientData *)userData;

    switch(event->type) {
    case TELNET_EV_SEND:
        send(client->conn_fd, event->data.buffer, event->data.size, 0);
        break;
    case TELNET_EV_DATA:
        if (telnet_rx_cb) {
            telnet_rx_cb(event->data.buffer, event->data.size);
        }
        break;
    default:
        break;
    }
}

static void telnet_handle_conn(int conn_fd) {
    static const telnet_telopt_t my_telopts[] = {
        { TELNET_TELOPT_ECHO,      TELNET_WONT, TELNET_DO   },
        { TELNET_TELOPT_TTYPE,     TELNET_WILL, TELNET_DONT },
        { TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DO   },
        { TELNET_TELOPT_ZMP,       TELNET_WONT, TELNET_DO   },
        { TELNET_TELOPT_MSSP,      TELNET_WONT, TELNET_DO   },
        { TELNET_TELOPT_BINARY,    TELNET_WILL, TELNET_DO   },
        { TELNET_TELOPT_NAWS,      TELNET_WILL, TELNET_DONT },
        { TELNET_TELOPT_LINEMODE,  TELNET_WONT, TELNET_DO   },
        { -1, 0, 0 }
    };

    UBaseType_t pending = 0;
    fd_set rfds, wfds;

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 200 * 1000,
    };

    struct telnetClientData *pClient = (struct telnetClientData *)calloc(1, sizeof(struct telnetClientData));
    if (!pClient) {
        ESP_LOGE(tag, "Failed to allocate memory for telnet client");
        return;
    }

    pClient->conn_fd  = conn_fd;
    pClient->tnHandle = telnet_init(my_telopts, telnet_event_handler, 0, pClient);
    if (!pClient->tnHandle) {
        ESP_LOGE(tag, "Failed to initialize telnet client");
        free(pClient);
        return;
    }

    while(1) {
        FD_ZERO(&rfds);
        FD_SET(conn_fd, &rfds);

        FD_ZERO(&wfds);
        vRingbufferGetInfo(telnet_buf_handle, NULL, NULL, NULL, NULL, &pending);
        if (pending) {
            FD_SET(conn_fd, &wfds);
        }

        int s = select(conn_fd + 1, &rfds, &wfds, NULL, &timeout);
        if (s < 0) {
            ESP_LOGE(tag, "select error: %d (%s)", errno, strerror(errno));
            break;
        }

        if (FD_ISSET(conn_fd, &rfds)) {
            int len = recv(conn_fd, pClient->rxbuf, TELNET_RX_BUF_SIZE, 0);
            if (len) {
                telnet_recv(pClient->tnHandle, pClient->rxbuf, len);
            } else {
                ESP_LOGE(tag, "telnet receive error: %d (%s)", errno, strerror(errno));
                break;
            }
        }

        if (FD_ISSET(conn_fd, &wfds)) {
            size_t size;
            void *item = xRingbufferReceiveUpTo(telnet_buf_handle, &size, pdMS_TO_TICKS(50), TELNET_TX_CHUNK_SIZE);
            if (item) {
                telnet_send_text(pClient->tnHandle, (char*)item, size);
                vRingbufferReturnItem(telnet_buf_handle, item);
            }
        }
      }

    telnet_free(pClient->tnHandle);
    free(pClient);
}

static void telnet_task(void *data) {
    int serverSocket;
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(23);

    while (1) {
        serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) >= 0 &&
            listen(serverSocket, 1) >= 0) {
            break;
        }

        close(serverSocket);
        ESP_LOGI(tag, "Unable to bind socket, retrying in 1s");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (1) {
        socklen_t len = sizeof(serverAddr);
        int conn_fd = accept(serverSocket, (struct sockaddr *)&serverAddr, &len);

        if (conn_fd >= 0) {
            ESP_LOGI(tag, "Telnet client connected");
            telnet_handle_conn(conn_fd);
            close(conn_fd);
            ESP_LOGI(tag, "Telnet client disconnected, telnet task HWM: %d", uxTaskGetStackHighWaterMark(NULL));
        } else {
            ESP_LOGW(tag, "Telnet socket accept error: %d (%s)", errno, strerror(errno));
        }
    }

    vTaskDelete(NULL);
}

void telnet_mirror_to_uart(bool enable) {
    mirror_to_uart = enable;
}

void start_telnet(void) {
    if (telnet_task_handle) {
        ESP_LOGW(tag, "Telnet task already running");
        return;
    }

    xTaskCreatePinnedToCore(telnet_task,
                            "telnet",
                            TELNET_TASK_STACK_SIZE,
                            NULL,
                            TELNET_TASK_PRIO,
                            &telnet_task_handle,
                            TELNET_TASK_CORE);
}

void init_telnet(telnet_rx_cb_t rx_cb) {
    telnet_rx_cb = rx_cb;
    telnet_buf_handle = xRingbufferCreate(TELNET_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (telnet_buf_handle == NULL) {
        ESP_LOGE(tag,"Failed to create ring buffer for telnet!");
        return;
    }

    ESP_LOGI(tag, "Redirecting log output to telnet");
    uart_fd = open("/dev/uart/0", O_RDWR);
    if (uart_fd < 0) {
        ESP_LOGE(tag, "Failed to open UART");
    }

    _GLOBAL_REENT->_stdout = fwopen(NULL, &telnet_write);
    _GLOBAL_REENT->_stderr = fwopen(NULL, &telnet_write);

    // enable line buffering for this stream (to be similar to the regular UART-based output)
    static char stdout_buf[128];
    static char stderr_buf[128];
    setvbuf(_GLOBAL_REENT->_stdout, stdout_buf, _IOLBF, sizeof(stdout_buf));
    setvbuf(_GLOBAL_REENT->_stderr, stderr_buf, _IOLBF, sizeof(stderr_buf));

    // Also redirect stdout/stderr of main task
    stdout=_GLOBAL_REENT->_stdout;
    stderr=_GLOBAL_REENT->_stderr;

    // Redirect the output of the ROM functions to our telnet handler
    ets_install_putc1(&telnet_putc);
}

ssize_t telnet_write(void *cookie, const char *data, ssize_t size) {
    (void)cookie;

    if (!data || !size) {
        return 0;
    }

    if (telnet_buf_handle) {
        int tries = 2;
        while (xRingbufferSend(telnet_buf_handle, data, size, 0) == pdFALSE && tries--) {
            size_t count;
            void *item = xRingbufferReceiveUpTo(telnet_buf_handle, &count, 0, size);
            if (item) {
                vRingbufferReturnItem(telnet_buf_handle, item);
            } else {
                break;
            }
        }
    }

    return (mirror_to_uart && uart_fd >= 0) ? write(uart_fd, data, size) : size;
}

void telnet_putc(char c) {
    static char buf[128];
    static size_t buf_pos = 0;
    buf[buf_pos] = c;
    buf_pos++;
    if (c == '\n' || buf_pos == sizeof(buf)) {
        telnet_write(NULL, buf, buf_pos);
        buf_pos = 0;
    }
}
