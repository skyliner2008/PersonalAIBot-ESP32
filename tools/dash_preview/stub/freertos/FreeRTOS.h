#pragma once
#include <stdint.h>
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
typedef unsigned UBaseType_t;
inline void xTaskNotifyGive(TaskHandle_t) {}  // the dash preview has no vehicle task
