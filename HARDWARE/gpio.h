#ifndef  __GPIO_H
#define  __GPIO_H
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
void PIN_Init(GPIO_TypeDef* GPIOx, uint32_t GPIO_Pin, GPIOMode_TypeDef GPIO_Mode, GPIOPuPd_TypeDef GPIO_PuPd);
#endif