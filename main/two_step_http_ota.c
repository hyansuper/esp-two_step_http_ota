#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#include <esp_http_server.h>
#include "esp_wifi.h"

static const char *TAG = "TWO_STEP_OTA";

static const char* index_page_header = "**************** app 0 *****************";

void ota_task_cb(void* p)
{
    esp_http_client_config_t config = {
        .url = p,
        // .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = 50000,
        .keep_alive_enable = true,
        // .skip_cert_common_name_check = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        // .http_client_init_cb = _http_client_init_cb, // Register a callback to be invoked after esp_http_client is initialized
        .partial_http_download = true,
        // .max_http_request_size = CONFIG_EXAMPLE_HTTP_REQUEST_SIZE,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        vTaskDelete(NULL);
    }

    // esp_app_desc_t app_desc;
    // err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "esp_https_ota_read_img_desc failed");
    //     goto ota_end;
    // }
    int total=esp_https_ota_get_image_size(https_ota_handle);
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        int r=esp_https_ota_get_image_len_read(https_ota_handle);
        ESP_LOGI(TAG, "Image bytes read: %d / %d, (%d%%)", r, total, r*100/total);
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG, "Complete data was not received.");
    } else {
        esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            // vTaskDelete(NULL);
            esp_restart();
        } else {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
            vTaskDelete(NULL);
        }
    }
// ota_end:
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed");
    vTaskDelete(NULL);

}

void connect_wifi() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());
}

esp_err_t get_ota_url(httpd_req_t* req, char* url, size_t len) {
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char* buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK
            && httpd_query_key_value(buf, "url", url, len) == ESP_OK) {

            ESP_LOGI(TAG, "OTA url = %s", url);

        } else {
            ESP_LOGI(TAG, "wrong query");
        }
        free(buf);
    }
    return ESP_OK;
}

esp_err_t ota_handler(httpd_req_t *req) {
    static char ota_url[256];
    if(get_ota_url(req, ota_url, sizeof(ota_url)) == ESP_OK) {

        xTaskCreate(&ota_task_cb, "ota_task", 1024 * 8, ota_url, 5, NULL);

        httpd_resp_send(req, "updating...", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send(req, "wrong query", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


esp_err_t index_handler(httpd_req_t *req) {
    char out[1024];
    char* resp = out;
    resp += sprintf(resp, "%s<br><br><br>", index_page_header);
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        resp += sprintf(resp, "App Partitoin: %s<br>project: %s<br>Version: %s", running->label, running_app_info.project_name, running_app_info.version);
    }
    char* other_ota_part = strcmp(running->label, "ota_0")?"ota_0":"ota_1";
    const esp_partition_t *data_part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, other_ota_part);
    if (data_part) {
        const void* map_ptr;
        esp_partition_mmap_handle_t map_handle;
        size_t skip_header = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
        ESP_ERROR_CHECK(esp_partition_mmap(data_part, skip_header, data_part->size-skip_header, ESP_PARTITION_MMAP_DATA, &map_ptr, &map_handle));
        // print first 10 letters in data partition
        resp += sprintf(resp, "<br><br>Data Partition: %s<br>data: %.10s", data_part->label, (char*)map_ptr);
        esp_partition_munmap(map_handle);
    }
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void start_server() {
    httpd_uri_t uri_ota = {
        .uri      = "/ota",
        .method   = HTTP_GET,
        .handler  = ota_handler,
    };
    httpd_uri_t uri_info = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_handler,
    };
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_register_uri_handler(server, &uri_ota);
    httpd_register_uri_handler(server, &uri_info);
}

void app_main(void)
{
    connect_wifi();

    start_server();

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                ESP_LOGI(TAG, "App is valid, rollback cancelled successfully");
            } else {
                ESP_LOGE(TAG, "Failed to cancel rollback");
            }
        }
    }

    vTaskDelete(NULL);
}
