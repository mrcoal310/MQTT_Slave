#include "stm32f4xx.h"
#include "gpio.h"
#include "OLED.h"
#include "Temp.h"
#include "wifi.h"
#include "tx_api.h"
#include <stdio.h>

#define LED_THREAD_STACK_SIZE      1024U
#define OLED_THREAD_STACK_SIZE     1024U
#define MQTT_THREAD_STACK_SIZE     8192U
#define KEY_THREAD_STACK_SIZE      1024U
#define KEY2_CLEAR_HOLD_TICKS      (TX_TIMER_TICKS_PER_SECOND * 3U)
#define THREADX_LOWEST_PRIORITY    ((1UL << __NVIC_PRIO_BITS) - 1UL)

static TX_THREAD led_thread;
static ULONG led_thread_stack[LED_THREAD_STACK_SIZE / sizeof(ULONG)];
static TX_THREAD oled_thread;
static ULONG oled_thread_stack[OLED_THREAD_STACK_SIZE / sizeof(ULONG)];
static TX_THREAD mqtt_thread;
static ULONG mqtt_thread_stack[MQTT_THREAD_STACK_SIZE / sizeof(ULONG)];
static TX_THREAD key_thread;
static ULONG key_thread_stack[KEY_THREAD_STACK_SIZE / sizeof(ULONG)];

extern unsigned int Image$$RW_IRAM1$$ZI$$Limit;
extern VOID *_tx_initialize_unused_memory;
extern VOID *_tx_thread_system_stack_ptr;

static void led_thread_entry(ULONG thread_input)
{
	(void)thread_input;

	while (1)
	{
		GPIO_ResetBits(GPIOF, GPIO_Pin_9);
		GPIO_SetBits(GPIOF, GPIO_Pin_10);
		tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 2U);

		GPIO_SetBits(GPIOF, GPIO_Pin_9);
		GPIO_ResetBits(GPIOF, GPIO_Pin_10);
		tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 2U);
	}
}

static char *wifi_state_text(WIFI_STATE state)
{
	switch (state)
	{
	case WIFI_STATE_ING:
		return "ING";
	case WIFI_STATE_OK:
		return "OK";
	case WIFI_STATE_AP:
		return "AP";
	case WIFI_STATE_FAIL:
		return "FAIL";
	case WIFI_STATE_IDLE:
	default:
		return "IDLE";
	}
}

static char *wifi_wifi_step_text(WIFI_WIFI_STEP step)
{
	switch (step)
	{
	case WIFI_WIFI_STEP_AT:
		return "AT";
	case WIFI_WIFI_STEP_MODE:
		return "MODE";
	case WIFI_WIFI_STEP_JOIN:
		return "JOIN";
	case WIFI_WIFI_STEP_AP:
		return "AP";
	case WIFI_WIFI_STEP_NONE:
	default:
		return "";
	}
}

static char *wifi_mqtt_step_text(WIFI_MQTT_STEP step)
{
	switch (step)
	{
	case WIFI_MQTT_STEP_CFG:
		return "CFG";
	case WIFI_MQTT_STEP_CONN:
		return "CONN";
	case WIFI_MQTT_STEP_SUB:
		return "SUB";
	case WIFI_MQTT_STEP_NONE:
	default:
		return "";
	}
}

static void oled_show_status(void)
{
	WIFI_STATUS status;
	char line[17];

	Wifi_GetStatus(&status);

	if ((status.wifi == WIFI_STATE_FAIL) && (status.wifi_step != WIFI_WIFI_STEP_NONE))
	{
		sprintf(line, "WiFi:%s FAIL", wifi_wifi_step_text(status.wifi_step));
	}
	else if ((status.wifi == WIFI_STATE_ING) && (status.wifi_step != WIFI_WIFI_STEP_NONE))
	{
		sprintf(line, "WiFi:%s ING ", wifi_wifi_step_text(status.wifi_step));
	}
	else
	{
		sprintf(line, "WiFi:%-11s", wifi_state_text(status.wifi));
	}
	OLED_ShowString(3, 1, line);

	if (status.wifi == WIFI_STATE_AP)
	{
		snprintf(line, sizeof(line), "SSID:%-11.11s", Wifi_GetApSsid());
		OLED_ShowString(4, 1, line);
	}
	else if (status.publish_enabled != 0U)
	{
		OLED_ShowString(4, 1, "DATA:ON         ");
	}
	else
	{
		if ((status.mqtt == WIFI_STATE_FAIL) && (status.mqtt_step != WIFI_MQTT_STEP_NONE))
		{
			sprintf(line, "MQTT:%s FAIL", wifi_mqtt_step_text(status.mqtt_step));
		}
		else if ((status.mqtt == WIFI_STATE_ING) && (status.mqtt_step != WIFI_MQTT_STEP_NONE))
		{
			sprintf(line, "MQTT:%s ING ", wifi_mqtt_step_text(status.mqtt_step));
		}
		else
		{
			sprintf(line, "MQTT:%-11s", wifi_state_text(status.mqtt));
		}
		OLED_ShowString(4, 1, line);
	}
}

static void oled_thread_entry(ULONG thread_input)
{
	uint8_t temperature;
	uint8_t humidity;
	ULONG last_temp_tick;
	ULONG now_tick;
	ULONG status_sleep_ticks;

	(void)thread_input;

	OLED_Init();
	Temp_Init();
	OLED_Clear();
	OLED_ShowString(1, 1, "Temperature:--");
	OLED_ShowString(2, 1, "Humidity:--");
	oled_show_status();
	Wifi_UpdateSensor(0U, 0U, 0U);
	tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND);
	last_temp_tick = tx_time_get() - (TX_TIMER_TICKS_PER_SECOND * 2U);
	status_sleep_ticks = (TX_TIMER_TICKS_PER_SECOND / 5U) > 0U ? (TX_TIMER_TICKS_PER_SECOND / 5U) : 1U;

	while (1)
	{
		now_tick = tx_time_get();
		if ((ULONG)(now_tick - last_temp_tick) >= (TX_TIMER_TICKS_PER_SECOND * 2U))
		{
			last_temp_tick = now_tick;
			if (Wifi_IsSwitchOn() == 0U)
			{
				Wifi_UpdateSensor(0U, 0U, 0U);
				OLED_ShowString(1, 1, "Temperature:--");
				OLED_ShowString(2, 1, "Humidity:--");
			}
			else if (Temp_Read(&temperature, &humidity) == TEMP_OK)
			{
				Wifi_UpdateSensor(temperature, humidity, 1U);
				OLED_ShowString(1, 1, "Temperature:");
				OLED_ShowNum(1, 13, temperature, 2);
				OLED_ShowString(2, 1, "Humidity:");
				OLED_ShowNum(2, 10, humidity, 2);
			}
			else
			{
				Wifi_UpdateSensor(0U, 0U, 0U);
				OLED_ShowString(1, 1, "Temperature:--");
				OLED_ShowString(2, 1, "Humidity:--");
			}
		}

		oled_show_status();
		tx_thread_sleep(status_sleep_ticks);
	}
}

static void key_thread_entry(ULONG thread_input)
{
	uint8_t key0_last = 0U;
	uint8_t key1_last = 0U;
	uint8_t key2_last = 0U;
	uint8_t wkup_last = 0U;
	uint8_t key0_now;
	uint8_t key1_now;
	uint8_t key2_now;
	uint8_t wkup_now;
	ULONG key2_press_tick = 0U;
	uint8_t key2_long_press_handled = 0U;

	(void)thread_input;

	while (1)
	{
		key0_now = (GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_4) == Bit_RESET) ? 1U : 0U;
		key1_now = (GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_3) == Bit_RESET) ? 1U : 0U;
		key2_now = (GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_2) == Bit_RESET) ? 1U : 0U;
		wkup_now = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_SET) ? 1U : 0U;

		if ((key0_now != 0U) && (key0_last == 0U))
		{
			Wifi_RequestWifiConnect();
		}

		if ((key1_now != 0U) && (key1_last == 0U))
		{
			Wifi_RequestMqttConnect();
		}

		if ((key2_now != 0U) && (key2_last == 0U))
		{
			key2_press_tick = tx_time_get();
			key2_long_press_handled = 0U;
		}

		if ((key2_now != 0U) &&
			(key2_long_press_handled == 0U) &&
			((ULONG)(tx_time_get() - key2_press_tick) >= KEY2_CLEAR_HOLD_TICKS))
		{
			Wifi_RequestClearConfig();
			key2_long_press_handled = 1U;
		}

		if ((key2_now == 0U) && (key2_last != 0U))
		{
			key2_press_tick = 0U;
			key2_long_press_handled = 0U;
		}

		if ((wkup_now != 0U) && (wkup_last == 0U))
		{
			Wifi_RequestPublishStart();
		}

		key0_last = key0_now;
		key1_last = key1_now;
		key2_last = key2_now;
		wkup_last = wkup_now;

		tx_thread_sleep((TX_TIMER_TICKS_PER_SECOND / 50U) > 0U ? (TX_TIMER_TICKS_PER_SECOND / 50U) : 1U);
	}
}

void _tx_initialize_low_level(void)
{
	SystemCoreClockUpdate();

	_tx_initialize_unused_memory = (VOID *)&Image$$RW_IRAM1$$ZI$$Limit;
	_tx_thread_system_stack_ptr = (VOID *)__get_MSP();

	SysTick_Config(SystemCoreClock / TX_TIMER_TICKS_PER_SECOND);
	NVIC_SetPriority(PendSV_IRQn, THREADX_LOWEST_PRIORITY);
	NVIC_SetPriority(SysTick_IRQn, THREADX_LOWEST_PRIORITY - 1UL);
}

void tx_application_define(VOID *first_unused_memory)
{
	(void)first_unused_memory;

	tx_thread_create(&led_thread,
					 "led_thread",
					 led_thread_entry,
					 0,
					 led_thread_stack,
					 LED_THREAD_STACK_SIZE,
					 1,
					 1,
					 TX_TIMER_TICKS_PER_SECOND / 10U,
					 TX_AUTO_START);

	tx_thread_create(&oled_thread,
					 "oled_thread",
					 oled_thread_entry,
					 0,
					 oled_thread_stack,
					 OLED_THREAD_STACK_SIZE,
					 2,
					 2,
					 TX_NO_TIME_SLICE,
					 TX_AUTO_START);

	tx_thread_create(&mqtt_thread,
					 "mqtt_thread",
					 Wifi_ThreadEntry,
					 0,
					 mqtt_thread_stack,
					 MQTT_THREAD_STACK_SIZE,
					 3,
					 3,
					 TX_NO_TIME_SLICE,
					 TX_AUTO_START);

	tx_thread_create(&key_thread,
					 "key_thread",
					 key_thread_entry,
					 0,
					 key_thread_stack,
					 KEY_THREAD_STACK_SIZE,
					 4,
					 4,
					 TX_NO_TIME_SLICE,
					 TX_AUTO_START);
}

int main()
{
	PIN_Init(GPIOF, GPIO_Pin_9, GPIO_Mode_OUT, GPIO_PuPd_NOPULL);
	PIN_Init(GPIOF, GPIO_Pin_10, GPIO_Mode_OUT, GPIO_PuPd_NOPULL);
	PIN_Init(GPIOE, GPIO_Pin_4, GPIO_Mode_IN, GPIO_PuPd_UP);
	PIN_Init(GPIOE, GPIO_Pin_3, GPIO_Mode_IN, GPIO_PuPd_UP);
	PIN_Init(GPIOE, GPIO_Pin_2, GPIO_Mode_IN, GPIO_PuPd_UP);
	PIN_Init(GPIOA, GPIO_Pin_0, GPIO_Mode_IN, GPIO_PuPd_DOWN);

	tx_kernel_enter();

	while (1)
	{
	}
}
