/**
 * @file WiFi functionality
 */

/* system includes */
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

/* local includes */
#include "tplink_kasa.h"
#include "wifi.h"

/* constants */
static const char *log_tag = "wifi";
static const uint32_t port = 9999;
static const uint8_t mac_address[] = {0xC0, 0xC9, 0xE3, 0xAD, 0x7C, 0x1D};

/* flag to indicate that server threads are running */
static bool server_running = false;

/* handles to server threads */
TaskHandle_t handle_tcp_server = NULL;
TaskHandle_t handle_udp_server = NULL;

/**
 * @brief Start TCP/UDP servers on port 9999
 */
void start_servers(void);

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ESP_LOGI(log_tag, "event ID %d", event_id);
    ESP_LOGD(log_tag, "HEAP free %d", esp_get_free_internal_heap_size());

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // wifi driver has started successfully, so attempt to connect to the configured wifi access point
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // ESP has disconnected from the wifi access point
        // the documentation suggests that in this event, the following is to be done:
        // 1) call esp_wifi_connect() to reconnect the Wi-Fi
        // 2) close all sockets
        // 3) re-create them if necessary
        ESP_LOGE(log_tag, "WiFi disconnected, reconnecting...");
        server_running = false;
        vTaskDelay(1000 / portTICK_RATE_MS);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // ESP has successfully connected to the configured wifi access point
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        start_servers();
        ESP_LOGI(log_tag, "ESP acquired IP address:" IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        // a wifi device has connected to the access point of the ESP
        start_servers();
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(log_tag, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

static esp_err_t configure_nvs_flash(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    return ret;
}

void wifi_setup(bool access_point)
{
    ESP_ERROR_CHECK(configure_nvs_flash());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    if ( access_point )
    {
        esp_netif_t *netif = esp_netif_create_default_wifi_ap();
        assert(netif);

        wifi_config_t wifi_config = {
            .ap = {
                .ssid = ACCESS_POINT_SSID,
                .ssid_len = strlen(ACCESS_POINT_SSID),
                .channel = 1,
                .max_connection = 1,
                .authmode = WIFI_AUTH_OPEN
            },
        };

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_AP, &mac_address[0]));
    }
    else
    {
        esp_netif_t *netif = esp_netif_create_default_wifi_sta();
        assert(netif);

        wifi_config_t wifi_config = {
            .sta = {
                .ssid = CONFIG_WIFI_SSID,
                .password = CONFIG_WIFI_PASSWORD,
                .scan_method = WIFI_FAST_SCAN,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                .threshold.rssi = -127,
                .threshold.authmode = WIFI_AUTH_OPEN,
            },
        };

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, &mac_address[0]));
    }
    
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void server_task(void *pvParameters)
{
    char addr_str[128];
    const int socket_type = (int)pvParameters;
    const bool is_tcp_server = socket_type == SOCK_STREAM;
    const bool is_udp_server = socket_type == SOCK_DGRAM;
    struct sockaddr_storage source_addr;
    socklen_t addr_len = sizeof(source_addr);
    struct sockaddr_storage dest_addr;
    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(port);

    /* TCP timeout settings */
    int keepAlive = 1;
    int keepIdle = 5;
    int keepInterval = 5;
    int keepCount = 3;

    /* allocate receive buffer */
    const int buffer_len = 2000;
    char * raw_buffer = malloc(buffer_len * sizeof(char));

    /* create TCP/UDP socket */
    int my_sock = socket(AF_INET, socket_type, IPPROTO_IP);
    if (my_sock < 0) {
        ESP_LOGE(log_tag, "Unable to create socket: errno %d", errno);
        return;
    }
    int opt = 1;
    setsockopt(my_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ESP_LOGI(log_tag, "Socket created");

    /* set UDP receive timeout */
    if (is_udp_server) {
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(my_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    }

    /* configure TCP socket as non-blocking */
    if (is_tcp_server) {
        fcntl(my_sock, F_SETFL, fcntl(my_sock, F_GETFL) | O_NONBLOCK);
    }

    /* bind to TCP/UDP port */
    int err = bind(my_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(log_tag, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(log_tag, "Socket bound, port %d", port);

    /* for TCP server, set socket into listening mode */
    if (is_tcp_server && (listen(my_sock, 1) != 0)) {
        ESP_LOGE(log_tag, "Error listening on TCP socket: errno %d", errno);
        goto CLEAN_UP;
    }

    /* receive loop */
    server_running = true;
    while (server_running)
    {
        int rx_len = 0;
        int connection = 0;

        /* for UDP server, periodically try to read a buffer of data from the socket */
        if (is_udp_server) {
            rx_len = recvfrom(my_sock, raw_buffer, buffer_len - 1, 0, (struct sockaddr *)&source_addr, &addr_len);
            if (rx_len < 0) {
                vTaskDelay(500 / portTICK_RATE_MS);
                continue;
            }
        }

        /* for TCP server, wait to accept client connection */
        if (is_tcp_server) {
            connection = accept(my_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (connection < 0) {
                vTaskDelay(500 / portTICK_RATE_MS);
                continue;
            }
            /* client connection has been accepted, kepp it alive */
            setsockopt(connection, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
            setsockopt(connection, IPPROTO_TCP, 5, &keepIdle, sizeof(int));
            setsockopt(connection, IPPROTO_TCP, 5, &keepInterval, sizeof(int));
            setsockopt(connection, IPPROTO_TCP, 3, &keepCount, sizeof(int));
            /* get a buffer of data */
            rx_len = recv(connection, raw_buffer, buffer_len - 1, 0);
            if (rx_len < 0) {
                ESP_LOGE(log_tag, "Error occurred during TCP receive: errno %d", errno);
                continue;
            } else if (rx_len == 0) {
                ESP_LOGI(log_tag, "Connection closed");
                continue;
            }
        }

        /* connection has now been made, so get the client IP address */
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(log_tag, "Connection from %s:%d/%s", addr_str, port, is_tcp_server ? "TCP" : "UDP");

        /* process the buffer and generate a response */
        int reply_len = tplink_kasa_process_buffer(raw_buffer, rx_len, is_tcp_server);

        /* send a response back to the client */
        ESP_LOGI(log_tag, "Replying with %d bytes", reply_len);
        if (is_udp_server) {
            int err = sendto(my_sock, raw_buffer, reply_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
            if (err < 0) {
                ESP_LOGE(log_tag, "Error occurred during UDP send: errno %d", errno);
            }
        }
        if (is_tcp_server) {
            int to_write = reply_len;
            while (to_write > 0) {
                int written = send(connection, raw_buffer + (reply_len - to_write), to_write, 0);
                if (written < 0) {
                    ESP_LOGE(log_tag, "Error occurred during TCP send: errno %d", errno);
                    break;
                }
                to_write -= written;
            }
            shutdown(connection, 0);
            close(connection);
        }
    }

CLEAN_UP:
    free(raw_buffer);
    close(my_sock);
    if (is_tcp_server) ESP_LOGI(log_tag, "TCP server ended");
    if (is_udp_server) ESP_LOGI(log_tag, "UDP server ended");
    vTaskDelete(NULL);
}

void start_servers(void)
{   
    /* start a TCP server on port 9999 for control commands (e.g. colour/on/off) */
    xTaskCreate(server_task, "tcp_server", 4096, (void*)SOCK_STREAM, 5, &handle_tcp_server);
    /* start a UDP server on port 9999 for get_sysinfo commands */
    xTaskCreate(server_task, "udp_server", 4096, (void*)SOCK_DGRAM, 5, &handle_udp_server);
}
