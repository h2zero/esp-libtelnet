#include "esp_system.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_mac.h" // ESP_MAC_WIFI_STA
#include "esp_console.h"

// component ${IDF_PATH}/examples/system/console/advanced/components/cmd_nvs
#include "cmd_nvs.h"
// component ${IDF_PATH}/examples/system/console/advanced/components/cmd_system
#include "cmd_system.h"
// component espressif/mdns
#include <mdns.h>
// component uqfus/esp32-wifi-provision-care https://github.com/uqfus/esp32-wifi-provision-care.git
#include "esp32-wifi-provision-care.h"
// component h2zero/esp-libtelnet: https://github.com/h2zero/esp-libtelnet.git
#include "esp-libtelnet.h"

static const char *TAG = "remote-console";

/*
 * We warn if a secondary serial console is enabled. A secondary serial console is always output-only and
 * hence not very useful for interactive console applications. If you encounter this warning, consider disabling
 * the secondary serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#error "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

static int wifi(int argc, char **argv)
{
    uint8_t mac_addr[6] = {0};
    //Get MAC address for WiFi Station interface
    ESP_ERROR_CHECK(esp_read_mac(mac_addr, ESP_MAC_WIFI_STA));
    printf("WIFI MAC: %02x-%02x-%02x-%02x-%02x-%02x\n",
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    printf("Associated AP SSID: %s\n", ap_info.ssid);
    printf("Associated AP MAC: %02x-%02x-%02x-%02x-%02x-%02x\n",
        ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2], ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
    printf("Wi-Fi channel: %d\n", ap_info.primary);
    printf("Wi-Fi RSSI: %d\n", ap_info.rssi);

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
    printf("IP address: " IPSTR "\n", IP2STR(&ip_info.ip));

    esp_ip6_addr_t ip6[5];
    int ip6_addrs = esp_netif_get_all_ip6(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), ip6);
    for (int j = 0; j < ip6_addrs; ++j)
    {
        esp_ip6_addr_type_t ipv6_type = esp_netif_ip6_get_addr_type(&(ip6[j]));
        printf("IPv6 address: " IPV6STR ", type: %d\n", IPV62STR(ip6[j]), ipv6_type);
    }
    return 0;
}

static void console_register_wifi(void)
{
    const esp_console_cmd_t cmd = {
        .command = "wifi",
        .help = "Print Wi-Fi information",
        .hint = NULL,
        .func = &wifi,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void telnet_rx_cb(const char *buf, size_t len)
{
    char cmdline[256+1]; // ESP_CONSOLE_CONFIG_DEFAULT() .max_cmdline_length = 256
    if (len >= sizeof(cmdline))
    {
        ESP_LOGI(TAG, "Error command too long.");
        return;
    }
    strlcpy(cmdline, buf, len + 1); // local copy with NUL-termination
    cmdline[strcspn(cmdline, "\r\n")] = 0; // remove LF, CR, CRLF, LFCR

    int ret;
    esp_err_t err = esp_console_run(cmdline, &ret);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("Unrecognized command\n");
    } else if (err == ESP_ERR_INVALID_ARG) {
        // no error, just cmdline was empty
    } else if (err == ESP_OK && ret != ESP_OK) {
        printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
    } else if (err != ESP_OK) {
        printf("Internal error: %s\n", esp_err_to_name(err));
    }
    printf(">");
    fflush(stdout);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Example start.");
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_provision_care((char *)TAG);

    ESP_LOGI(TAG, "Publish mDNS hostname %s.local.", TAG);
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(TAG));

    /* Initialize the console */
    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    esp_console_register_help_command(); // Default 'help' command prints the list of registered commands

    register_system_common();  // examples/system/console/advanced/components/cmd_system
    register_nvs(); // examples/system/console/advanced/components/cmd_nvs
    console_register_wifi();

    //Initialize telnet
    init_telnet(telnet_rx_cb);
    start_telnet();
    ESP_LOGI(TAG, "Telnet server up and ready for connections. Type 'help' to print available commands.");
    printf(">");
    fflush(stdout);

    vTaskSuspend(NULL); // Start done.
    vTaskDelete(NULL); // Task functions should never return.
}
