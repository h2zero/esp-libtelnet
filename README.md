# esp-libtelnet
A library for providing a remote console over Telnet, and (optionally) UART, for esp32 devices.

## Features
- Remote monitor access from any Telnet client.
- Optional output mirroring to UART.
- Log output from `printf`, `stdout`, `stderr`, and `ets_printf` are all provided.

## Using
Clone this repo to your project/components folder and add `#include <esp-libtelnet.h>` to your project file.

Alternatively, this can be installed via IDF Component Manager by adding the following to your project idf_component.yml file:
```
  h2zero/esp-libtelnet:
    git: "https://github.com/h2zero/esp-libtelnet"
```

## API
- `void init_telnet(telnet_rx_cb_t rx_cb)` Initialize the Telnet data structures. Param: `rx_cb` takes a callback function of `typedef void (*telnet_rx_cb_t)(const char *buf, size_t len)`.
- `void start_telnet(void)` Starts the Telnet task which listens for clients, do no call until an IP address has been received.
- `void telnet_mirror_to_uart(bool enable)` Enable (true) or disable (false) mirroring console output to UART.

## Aknowledgements
This library uses [libtelnet @commit 5f5ecee](https://github.com/seanmiddleditch/libtelnet) by Sean Middleditch and contributors
