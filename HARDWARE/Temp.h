#ifndef __TEMP_H
#define __TEMP_H

#include "stm32f4xx.h"

#define TEMP_OK      0U
#define TEMP_ERROR   1U

void Temp_Init(void);
uint8_t Temp_Read(uint8_t *temperature, uint8_t *humidity);

#endif
