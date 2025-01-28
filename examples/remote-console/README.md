# remote-console
Example of telnet connection used to run console commands on esp32.

## Using

Build and flash esp32.
Esp32 will spawn Access Point with Captive Portal.
Provide Wi-Fi SSID and Wi-Fi password to connect to your Wi-Fi.
Use putty or any other telnet client to connect to esp32 console.
Host Name: 'remote-console.local' Port: '23' Connection Type 'telnet'

Once connected use 'help' command to print available commands in esp32 console.