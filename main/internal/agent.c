#include "internal/gemini.h"

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "gemini.h"
#include "scan_bluetooth.h"
#include "scan_wifi.h"
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

static const char *TAG = "agent";

// Eight each
#define MAX_MESSAGES 16

Message messages[MAX_MESSAGES];
int message_end = 0;
int message_start = 0;

// Copies the strings so everything is handled
void push_message(Message message) {
  message.role = strdup(message.role);
  message.text = strdup(message.text);

  int next_end = (message_end + 1) % MAX_MESSAGES;

  if (next_end == message_start) {
    ESP_LOGW(TAG, "Buffer full! Overwriting oldest message at index %d.",
             message_start);
    message_start = (message_start + 1) % MAX_MESSAGES;

    free(messages[next_end].role);
    free(messages[next_end].text);
  }

  messages[next_end] = message;

  message_end = next_end;
}

cJSON *get_chat() {
  cJSON *chat = init_chat();
  if (chat == NULL) {
    ESP_LOGE(TAG, "Failed to make chat");
    return NULL;
  }

  if (add_message(
          chat,
          (Message){
              "user",
              "SYSTEM: You are a tiny handheld screen that helps users "
              "find and understand devices around them. When you say "
              "technical things such as hex provide a description of "
              "what it could mean. The user is technically informed and knows "
              "what hex codes are. Keep things simple and under 400 "
              "characters per response. NO EMOJIS"}) < 0) {
    ESP_LOGE(TAG, "Failed to add prompt");
    cJSON_Delete(chat);
    return NULL;
  }

  char *str = wifi_scan_to_prompt();
  if (add_message(chat, (Message){"user", str}) < 0) {
    ESP_LOGE(TAG, "Failed to add prompt");
    free(str);
    cJSON_Delete(chat);
    return NULL;
  }
  free(str);

  str = blue_scan_to_prompt();
  if (add_message(chat, (Message){"user", str}) < 0) {
    ESP_LOGE(TAG, "Failed to add prompt");
    free(str);
    cJSON_Delete(chat);
    return NULL;
  }
  free(str);

  for (int i = message_start; i = (i + 1) % MAX_MESSAGES;) {
    if (add_message(chat, messages[i]) < 0) {
      ESP_LOGE(TAG, "Failed to add message");
      cJSON_Delete(chat);
      return NULL;
    }

    if (i == message_end) {
      break;
    }
  }

  return chat;
}

// Input is not freed
char *get_message(char *input) {
  push_message((Message){"user", input});

  cJSON *chat = get_chat();
  if (chat == NULL) {
    ESP_LOGE(TAG, "Failed to make chat");
    return NULL;
  }

  char *res = call_gemini(chat);
  cJSON_Delete(chat);

  if (res == NULL) {
    ESP_LOGE(TAG, "Failed to get gemini response");
    return NULL;
  }

  push_message((Message){"model", res});

  return res;
}
