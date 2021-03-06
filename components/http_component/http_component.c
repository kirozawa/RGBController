#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <esp_system.h>
#include <sys/param.h>
#include "cJSON.h"

#include "led_control.h"
#include "statemachine.h"


#include "http_component.h"

#include "global.h"

#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

bool server_started = false;

static const char *TAG="HTTP Component";

sLedStripConfig_t led_config_struct;

sLedStripConfig_t colors;
httpd_handle_t server_instance;

/* An HTTP GET handler */
esp_err_t api_get_color_handler(httpd_req_t *req)
{
    char* resp_str = malloc(64);

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */

    colors = get_led_state();
    

    
    sprintf(resp_str,"%s",jsonify_colors());
    httpd_resp_send(req, resp_str, strlen(resp_str));

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    free(resp_str);
    return ESP_OK;
}

httpd_uri_t api_get_color_handler_s = {
    .uri       = "/api/GetColor",
    .method    = HTTP_GET,
    .handler   = api_get_color_handler,
};

esp_err_t api_set_color_handler(httpd_req_t *req)
{
    char buf[100];
    char* resp_str = malloc(64);
    int ret = 0, remaining = req->content_len;

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf,
                        MIN(remaining, sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }

        remaining -= ret;

    }
    if (ret != 0){
        buf[ret] = '\0';
        // End response
        // httpd_resp_send_chunk(req, NULL, 0);
        ESP_LOGI(TAG, "Parsing Request to json");
        cJSON *json = cJSON_Parse(buf);
        cJSON *colors;
        colors = cJSON_GetObjectItem(json, "colors");
        int red = cJSON_GetObjectItem(colors, "red")->valueint;
        int green = cJSON_GetObjectItem(colors, "green")->valueint;
        int blue = cJSON_GetObjectItem(colors, "blue")->valueint;

        ESP_LOGI(TAG, "r[%d]g[%d]b[%d]",red,green,blue);


        led_config_struct.mode = LED_MODE_STATIC;
        led_config_struct.brightness = 0;
        led_config_struct.channel[LEDC_R].hex_val = red;
        led_config_struct.channel[LEDC_G].hex_val = green;
        led_config_struct.channel[LEDC_B].hex_val = blue;
        change_mode(&led_config_struct);

        /*TODO:Send response if changemode was correct*/
        sprintf(resp_str,"%s","{status: ok}");
        httpd_resp_send(req, resp_str, strlen(resp_str));

        return ESP_OK;
    }

    return ESP_FAIL;
    
}

httpd_uri_t api_set_color_handler_s = {
    .uri       = "/api/SetColor",
    .method    = HTTP_POST,
    .handler   = api_set_color_handler,
};

esp_err_t api_set_creds_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;
    char* resp_str = malloc(64);
    char ssid[32],passwd[32];
    memset(ssid,0,sizeof(ssid));
    memset(passwd,0,sizeof(passwd));
   

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => ssid=%s", ssid);
                
                if(sizeof(ssid) <= sizeof(global_ssid)){
                    memcpy(global_ssid, ssid, sizeof(ssid));
                }
            }
            if (httpd_query_key_value(buf, "passwd", passwd, sizeof(passwd)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => passwd=%s", passwd);

                if(sizeof(passwd) <= sizeof(global_passwd)){
                    memcpy(global_passwd, passwd, sizeof(passwd));
                }
                
            }
        }
        free(buf);
    }
    /*Chek if got both */
    if(ssid[0] && passwd[0]){
        sprintf(resp_str,"%s","{status: ok}");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        set_system_state(STATE_AP_GOT_CONFIG);
    }
    else{
        sprintf(resp_str,"%s","{status: fail}");
        httpd_resp_send(req, resp_str, strlen(resp_str));
    }
    free(resp_str);

    return ESP_OK;
}


httpd_uri_t api_set_creds_handler_s = {
    .uri       = "/api/setCreds",
    .method    = HTTP_GET,
    .handler   = api_set_creds_handler,
};


httpd_handle_t start_webserver(void)
{
    if(server_started){
        stop_webserver(server_instance);
        vTaskDelay(5000);
    }
    server_started = true;
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_HTTP_REST_PORT;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &api_get_color_handler_s);
        httpd_register_uri_handler(server, &api_set_creds_handler_s);
        httpd_register_uri_handler(server, &api_set_color_handler_s);
        server_instance = server;
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}




