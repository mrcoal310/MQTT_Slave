#ifndef __WIFI_H
#define __WIFI_H

#include "stm32f4xx.h"
#include "tx_api.h"

typedef enum
{
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_ING,
    WIFI_STATE_OK,
    WIFI_STATE_AP,
    WIFI_STATE_FAIL
} WIFI_STATE;

typedef enum
{
    WIFI_WIFI_STEP_NONE = 0,
    WIFI_WIFI_STEP_AT,
    WIFI_WIFI_STEP_MODE,
    WIFI_WIFI_STEP_JOIN,
    WIFI_WIFI_STEP_AP
} WIFI_WIFI_STEP;

typedef enum
{
    WIFI_MQTT_STEP_NONE = 0,
    WIFI_MQTT_STEP_CFG,
    WIFI_MQTT_STEP_CONN,
    WIFI_MQTT_STEP_SUB
} WIFI_MQTT_STEP;

typedef struct
{
    WIFI_STATE wifi;
    WIFI_STATE mqtt;
    WIFI_WIFI_STEP wifi_step;
    WIFI_MQTT_STEP mqtt_step;
    uint8_t publish_enabled;
} WIFI_STATUS;

void Wifi_Init(void);
void Wifi_ThreadEntry(ULONG thread_input);
void Wifi_USART3_IRQHandler(void);
void Wifi_GetStatus(WIFI_STATUS *status);
const char *Wifi_GetApSsid(void);
void Wifi_UpdateSensor(uint8_t temperature, uint8_t humidity, uint8_t valid);
uint8_t Wifi_IsSwitchOn(void);
void Wifi_RequestWifiConnect(void);
void Wifi_RequestMqttConnect(void);
void Wifi_RequestPublishStart(void);
void Wifi_RequestClearConfig(void);

#endif
