#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/cyw43_arch.h"
#include "pico/async_context.h"
#include "lwip/altcp_tls.h"
#include "ssh1106.h"
#include "example_http_client_util.h"
#include <string.h>
#include <stdio.h>

static void makeGetRequestTest();

ssh1106_t display = {};
const int sda_pin = 16;
const int scl_pin = 17;

int main() {
    stdio_init_all();

    // Initialize display
    if(!ssh1106_init(&display, i2c0, sda_pin, scl_pin)) {
       stdio_printf("Failed to initialize display");
       return 1;
    }
    
    ssh1106_clear(&display);
    ssh1106_draw_string(&display, 10, 10, "Initializing...", SSH1106_COLOR_WHITE);
    ssh1106_update(&display);

    // Initialize WiFi chip
    if (cyw43_arch_init()) {
        printf("WiFi init failed\n");
        ssh1106_clear(&display);
        ssh1106_draw_string(&display, 0, 10, "WiFi init failed", SSH1106_COLOR_WHITE);
        ssh1106_update(&display);
        return 1;
    }

    ssh1106_clear(&display);
    ssh1106_draw_string(&display, 10, 10, "Connecting WiFi", SSH1106_COLOR_WHITE);
    ssh1106_update(&display);

    cyw43_arch_enable_sta_mode();
    printf("Attempting WiFi connection...\n");
    printf("SSID: %s\n", WIFI_SSID);
    printf("Password length: %d\n", strlen(WIFI_PASSWORD));

    int status = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000);
    if (status != 0) {
        printf("WiFi connection failed with status: %d\n", status);
        ssh1106_clear(&display);
        ssh1106_draw_string(&display, 0, 10, "WiFi failed", SSH1106_COLOR_WHITE);
        char status_str[20];
        snprintf(status_str, sizeof(status_str), "Status: %d", status);
        ssh1106_draw_string(&display, 0, 25, status_str, SSH1106_COLOR_WHITE);
        ssh1106_update(&display);
        return 1;
    }

    printf("WiFi connected!\n");
    ssh1106_clear(&display);
    ssh1106_draw_string(&display, 10, 10, "WiFi Connected!", SSH1106_COLOR_WHITE);
    ssh1106_update(&display);

    while (1) {
        sleep_ms(5000);
        makeGetRequestTest();
    }
}

static void makeGetRequestTest() {
    EXAMPLE_HTTP_REQUEST_T req = {0};
    req.hostname = "api.spotify.com";
    req.url = "/v1/me/player/currently-playing";
    req.headers_fn = http_client_header_print_fn;
    req.recv_fn = http_client_receive_print_fn;
    req.tls_config = altcp_tls_create_config_client(NULL, 0);  // HTTPS

    int result = http_client_request_sync(cyw43_arch_async_context(), &req);
    printf("result: %d\n", result);
    altcp_tls_free_config(req.tls_config);
}