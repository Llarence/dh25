#ifndef GEMINI_H
#define GEMINI_H

#include "cJSON.h"

typedef struct Message {
  char *role;
  char *text;
} Message;

cJSON *init_chat(void);
int add_message(cJSON *request, Message message);
char *call_gemini(cJSON *chat);

#endif
