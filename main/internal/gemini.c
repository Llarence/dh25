#include "internal/gemini.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "secrets.h"
#include <stddef.h>
#include <unistd.h>

static const char *TAG = "gemini";

#define GEMINI_ENDPOINT                                                        \
  "https://generativelanguage.googleapis.com/v1beta/models/"                   \
  "gemini-2.5-flash:generateContent?key=" API_KEY

extern const uint8_t google_cert[] asm("_binary_google_cert_pem_start");
extern const uint8_t google_cert_end[] asm("_binary_google_cert_pem_end");

cJSON *init_chat(void) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to create root cJSON object.");
    return NULL;
  }

  cJSON *contents_array = cJSON_CreateArray();
  if (contents_array == NULL) {
    ESP_LOGE(TAG, "Failed to create contents array.");
    cJSON_Delete(root);
    return NULL;
  }

  if (!cJSON_AddItemToObject(root, "contents", contents_array)) {
    ESP_LOGE(TAG, "Failed to add contents array to root object.");
    cJSON_Delete(root);
    return NULL;
  }

  return root;
}

int add_message(cJSON *request, Message message) {
  if (request == NULL || message.role == NULL || message.text == NULL) {
    ESP_LOGE(TAG, "Invalid input parameters (request, role, or text is NULL).");
    return -1;
  }

  cJSON *contents_array = cJSON_GetObjectItemCaseSensitive(request, "contents");
  if (contents_array == NULL || !cJSON_IsArray(contents_array)) {
    ESP_LOGE(TAG, "Request object does not contain a valid 'contents' array.");
    return -1;
  }

  cJSON *message_object = cJSON_CreateObject();
  if (message_object == NULL) {
    ESP_LOGE(TAG, "Failed to create message object.");
    return -1;
  }

  if (!cJSON_AddStringToObject(message_object, "role", message.role)) {
    ESP_LOGE(TAG, "Failed to add role '%s' to message object.", message.role);
    cJSON_Delete(message_object);
    return -1;
  }

  cJSON *parts_array = cJSON_CreateArray();
  cJSON *text_part = cJSON_CreateObject();

  if (parts_array == NULL || text_part == NULL) {
    ESP_LOGE(TAG, "Failed to create parts or text_part object.");
    cJSON_Delete(message_object);
    return -1;
  }

  if (!cJSON_AddStringToObject(text_part, "text", message.text)) {
    ESP_LOGE(TAG, "Failed to add text to text_part.");
    cJSON_Delete(message_object);
    return -1;
  }

  cJSON_AddItemToArray(parts_array, text_part);

  if (!cJSON_AddItemToObject(message_object, "parts", parts_array)) {
    ESP_LOGE(TAG, "Failed to add parts array to message object.");
    cJSON_Delete(message_object);
    return -1;
  }

  cJSON_AddItemToArray(contents_array, message_object);

  return 0;
}

char *call_gemini(cJSON *chat) {
  char *request_data = cJSON_Print(chat);
  if (request_data == NULL) {
    ESP_LOGE(TAG, "Failed to add parts array to message object.");
    return NULL;
  }

  ESP_LOGI(TAG, "Gemini POST with: %s", request_data);

  esp_http_client_config_t config = {
      .url = GEMINI_ENDPOINT,
      .method = HTTP_METHOD_POST,
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .cert_pem = (const char *)google_cert,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);

  esp_http_client_set_header(client, "Content-Type", "application/json");

  size_t request_len = strlen(request_data);
  esp_err_t err = esp_http_client_open(client, request_len);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    free(request_data);
    return NULL;
  }

  int write_len = esp_http_client_write(client, request_data, request_len);
  if (write_len < 0) {
    ESP_LOGE(TAG, "Error writing request body");
  } else if (write_len != request_len) {
    ESP_LOGW(TAG, "Wrote only %d of %zu bytes of request data", write_len,
             request_len);
  }

  int body_read_len = esp_http_client_fetch_headers(client);

  cJSON *json = NULL;
  if (body_read_len >= 0) {
    ESP_LOGI(TAG, "Gemini POST request successful. Status: %d",
             esp_http_client_get_status_code(client));

    int response_len = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "Gemini Response Length: %d (May be -1 for chunked response)",
             response_len);

    const int MAX_RESPONSE_SIZE = 1024;
    char *response_buffer = (char *)malloc(MAX_RESPONSE_SIZE + 1);
    int total_bytes_read = 0;

    if (response_buffer) {
      int bytes_read = 0;
      while ((bytes_read = esp_http_client_read(
                  client, response_buffer + total_bytes_read,
                  MAX_RESPONSE_SIZE - total_bytes_read)) > 0) {
        total_bytes_read += bytes_read;
        if (total_bytes_read >= MAX_RESPONSE_SIZE) {
          ESP_LOGW(TAG, "Response truncated: Buffer limit reached");
          break;
        }
      }

      if (bytes_read < 0) {
        ESP_LOGE(TAG, "Error reading response: %s",
                 esp_err_to_name(bytes_read));
      }

      response_buffer[total_bytes_read] = '\0';

      ESP_LOGI(TAG, "Total Bytes Read: %d", total_bytes_read);
      ESP_LOGI(TAG, "Gemini Response Body: %s", response_buffer);

      // TODO: Maybe use with length
      json = cJSON_Parse(response_buffer);
      free(response_buffer);
    } else {
      ESP_LOGE(TAG, "Failed to allocate memory for response buffer.");
    }

  } else {
    ESP_LOGE(TAG, "Gemini POST request failed to get headers. Error: %s",
             esp_err_to_name(body_read_len));
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  free(request_data);

  if (json == NULL) {
    ESP_LOGE(TAG, "Couldn't parse string to json");
    return NULL;
  }

  char *text = NULL;
  cJSON *candidates = cJSON_GetObjectItemCaseSensitive(json, "candidates");
  if (cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {

    cJSON *first_candidate = cJSON_GetArrayItem(candidates, 0);
    if (cJSON_IsObject(first_candidate)) {

      cJSON *content =
          cJSON_GetObjectItemCaseSensitive(first_candidate, "content");
      if (cJSON_IsObject(content)) {

        cJSON *parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
        if (cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {

          cJSON *first_part = cJSON_GetArrayItem(parts, 0);
          if (cJSON_IsObject(first_part)) {

            cJSON *text_item =
                cJSON_GetObjectItemCaseSensitive(first_part, "text");
            if (cJSON_IsString(text_item) && text_item->valuestring != NULL) {
              text = strdup(text_item->valuestring);
            }
          }
        }
      }
    }
  }

  if (text == NULL) {
    ESP_LOGE(TAG, "Couldn't parse json");
  }

  cJSON_Delete(json);
  return text;
}
