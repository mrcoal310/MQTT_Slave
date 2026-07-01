#include "wifi.h"
#include "cJSON.h"
#include "stm32f4xx_flash.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define WIFI_STA_MODE             1U
#define WIFI_AP_MODE              2U
#define WIFI_RX_BUFFER_SIZE       1024U
#define WIFI_LINE_BUFFER_SIZE     512U
#define WIFI_CMD_BUFFER_SIZE      768U
#define WIFI_HTTP_BUFFER_SIZE     2048U
#define WIFI_DEFAULT_REPORT_PERIOD_S  5U
#define WIFI_REPORT_PERIOD_MIN_S      1U
#define WIFI_REPORT_PERIOD_MAX_S      3600U
#define WIFI_HEARTBEAT_PERIOD_S       30U
#define WIFI_RSSI_REFRESH_PERIOD_S    5U
#define WIFI_STA_SSID_MAX_LEN     32U
#define WIFI_STA_PASSWORD_MAX_LEN 64U
#define WIFI_SWITCH_DEFAULT_ON    1U
#define WIFI_AP_SSID_PREFIX       "Sen_"
#define WIFI_AP_SSID_FALLBACK     WIFI_AP_SSID_PREFIX "0000"
#define WIFI_AP_PASSWORD          "12345678"
#define WIFI_AP_CHANNEL           6U
#define WIFI_AP_ENCRYPTION        3U
#define WIFI_CONFIG_FLASH_ADDRESS 0x080E0000UL
#define WIFI_CONFIG_FLASH_SECTOR  FLASH_Sector_11
#define WIFI_CONFIG_FLASH_VOLTAGE_RANGE VoltageRange_3
#define WIFI_CONFIG_MAGIC         0x57494346UL
#define WIFI_CONFIG_VERSION       1UL
#define WIFI_PRODUCT_KEY          "yourkey"
#define WIFI_DEVICE_NAME          "yourdevicename"
#define APP_PRODUCT_ID            "envctrl_v1"
#define APP_DEVICE_ID             "iot_node_001"
#define WIFI_PROTOCOL_VERSION     "1.0"
#define WIFI_FW_VERSION           "1.0.0"
#define WIFI_MQTT_QOS0            0U
#define WIFI_MQTT_QOS1            1U
#define WIFI_ENABLE_LEGACY_ALIYUN_TSL  0U
#define WIFI_ENABLE_CUSTOM_CMD_TOPIC   1U
#define WIFI_ENABLE_CUSTOM_AUX_TOPICS  1U
#define WIFI_CUSTOM_TOPIC_DATA         "DATA"
#define WIFI_DEFAULT_RSSI         (-60)
#define WIFI_DEFAULT_TEMP_HIGH_LIMIT   30
#define WIFI_DEFAULT_HUMIDITY_HIGH_LIMIT 80
#define WIFI_TIMER_MAX_DELAY_S         86400
#define WIFI_RESTART_DELAY_MS          200U
#define WIFI_RETRY_FAST_INTERVAL_MS    5000U
#define WIFI_RETRY_SLOW_INTERVAL_MS    60000U
#define WIFI_RETRY_FAST_ATTEMPTS       5U
#define WIFI_ERROR_OK                  0U
#define WIFI_ERROR_JSON_PARSE          1001U
#define WIFI_ERROR_MISSING_FIELD       1002U
#define WIFI_ERROR_INVALID_PARAM       1003U
#define WIFI_ERROR_UNSUPPORTED_CMD     1004U
#define WIFI_ERROR_SENSOR              2001U
#define WIFI_ERROR_GPIO                2002U
#define WIFI_ERROR_NETWORK             3001U
#define ALIYUN_PROP_TEMPERATURE   "temperature"
#define ALIYUN_PROP_HUMIDITY      "humidity"
#define ALIYUN_PROP_SWITCH        "switch"
#define ALIYUN_PROP_UPTIME        "uptime"
#define ALIYUN_PROP_TIMER_REMAIN  "timer_remain"
#define ALIYUN_PROP_REPORT_PERIOD "report_period_s"

static const char *serverPort = "1883";
static const char *clientId = WIFI_PRODUCT_KEY "." WIFI_DEVICE_NAME "|securemode=2\\,signmethod=hmacsha256\\,timestamp=1779363405195|";
static const char *passwd = "yourpassword";
static const char *mqttHostUrl = "yoururl";
static const char *username = WIFI_DEVICE_NAME "&" WIFI_PRODUCT_KEY;
#if WIFI_ENABLE_LEGACY_ALIYUN_TSL
static const char *legacyPublishTopic = "/sys/" WIFI_PRODUCT_KEY "/" WIFI_DEVICE_NAME "/thing/event/property/post";
static const char *legacySubscribeTopic = "/sys/" WIFI_PRODUCT_KEY "/" WIFI_DEVICE_NAME "/thing/service/property/set";
#endif
static const char *telemetryTopic = "/" WIFI_PRODUCT_KEY "/" WIFI_DEVICE_NAME "/user/" WIFI_CUSTOM_TOPIC_DATA;
#if WIFI_ENABLE_CUSTOM_AUX_TOPICS
static const char *statusTopic = "/" WIFI_PRODUCT_KEY "/" WIFI_DEVICE_NAME "/user/status";
static const char *replyTopic = "/" WIFI_PRODUCT_KEY "/" WIFI_DEVICE_NAME "/user/reply";
static const char *eventTopic = "/" WIFI_PRODUCT_KEY "/" WIFI_DEVICE_NAME "/user/event";
static const char *heartbeatTopic = "/" WIFI_PRODUCT_KEY "/" WIFI_DEVICE_NAME "/user/heartbeat";
#endif
#if WIFI_ENABLE_CUSTOM_CMD_TOPIC
static const char *cmdTopic = "/" WIFI_PRODUCT_KEY "/" WIFI_DEVICE_NAME "/user/cmd";
#endif

static volatile uint16_t wifi_rx_head = 0U;
static volatile uint16_t wifi_rx_tail = 0U;
static uint8_t wifi_rx_buffer[WIFI_RX_BUFFER_SIZE];

typedef struct
{
    uint32_t magic;
    uint32_t version;
    char ssid[WIFI_STA_SSID_MAX_LEN + 1U];
    char password[WIFI_STA_PASSWORD_MAX_LEN + 1U];
    uint16_t reserved;
    uint32_t checksum;
} WIFI_STA_CONFIG;

static WIFI_STATE wifi_state = WIFI_STATE_IDLE;
static WIFI_STATE mqtt_state = WIFI_STATE_IDLE;
static WIFI_WIFI_STEP wifi_step = WIFI_WIFI_STEP_NONE;
static WIFI_MQTT_STEP mqtt_step = WIFI_MQTT_STEP_NONE;
static uint8_t wifi_request = 1U;
static uint8_t mqtt_request = 1U;
static uint8_t clear_config_request = 0U;
static uint8_t publish_request = 0U;
static uint8_t publish_enabled = 0U;
static uint8_t switch_state = 0U;
static uint8_t sensor_temperature = 0U;
static uint8_t sensor_humidity = 0U;
static uint8_t sensor_valid = 0U;
static uint8_t timer_enable = 0U;
static uint8_t timer_target_switch = 0U;
static uint8_t auto_rule_enable = 0U;
static uint8_t auto_rule_active = 0U;
static uint8_t restart_pending = 0U;
static ULONG timer_deadline = 0U;
static ULONG restart_deadline = 0U;
static ULONG report_period_seconds = WIFI_DEFAULT_REPORT_PERIOD_S;
static ULONG last_rssi_update_tick = 0U;
static unsigned int seq = 0U;
static int wifi_rssi = WIFI_DEFAULT_RSSI;
static int temp_high_limit = WIFI_DEFAULT_TEMP_HIGH_LIMIT;
static int humidity_high_limit = WIFI_DEFAULT_HUMIDITY_HIGH_LIMIT;
static uint8_t wifi_portal_active = 0U;
static uint8_t wifi_sta_config_valid = 0U;
static uint8_t wifi_retry_fail_count = 0U;
static ULONG wifi_retry_deadline = 0U;
static char wifi_ap_ssid[WIFI_STA_SSID_MAX_LEN + 1U] = WIFI_AP_SSID_FALLBACK;
static WIFI_STA_CONFIG wifi_sta_config;

static const char *const wifi_switch_cmd_aliases[] = {"switch_set", "switch", "set_switch"};
static const char *const wifi_switch_on_aliases[] = {"open_device", "device_on", "turn_on", "start_device", "switch_on", "on"};
static const char *const wifi_switch_off_aliases[] = {"close_device", "device_off", "turn_off", "stop_device", "switch_off", "off"};
static const char *const wifi_restart_aliases[] = {"restart", "restart_device", "reboot", "device_restart"};
static const char *const wifi_status_query_aliases[] = {"status_query", "query_status", "get_status", "query_remain", "remain_query", "timer_query", "query_timer", "query"};
static const char *const wifi_timer_set_aliases[] = {"timer_set", "delay_set", "set_timer", "set_delay", "delay_on", "delay_off"};
static const char *const wifi_timer_cancel_aliases[] = {"timer_cancel", "cancel_timer", "cancel_delay", "timer_clear"};
static const char *const wifi_config_set_aliases[] = {"config_set", "set_config", "config", "update_config"};
static const char *const wifi_action_on_aliases[] = {"on", "open", "enable", "start", "delay_on", "open_device"};
static const char *const wifi_action_off_aliases[] = {"off", "close", "disable", "stop", "delay_off", "close_device"};
static const char *const wifi_switch_keys[] = {"switch", "switch_state", "state", "value"};
static const char *const wifi_delay_keys[] = {"delay_s", "delay", "seconds", "timer_s", "remain_s"};
static const char *const wifi_action_keys[] = {"action", "timer_action", "mode"};
static const char *const wifi_report_period_keys[] = {"report_period_s", "report_period", "upload_period", "period"};
static const char *const wifi_temp_limit_keys[] = {"temp_high_limit", "temperature_high_limit", "temp_limit"};
static const char *const wifi_humidity_limit_keys[] = {"humidity_high_limit", "humidity_limit", "humid_limit"};
static const char *const wifi_auto_rule_keys[] = {"auto_rule_enable", "auto_rule", "rule_enable"};

static void wifi_publish_telemetry(void);
static void wifi_publish_status(uint8_t online, const char *mqtt_text);
static void wifi_publish_event(const char *event_type, const char *message);
#if WIFI_ENABLE_LEGACY_ALIYUN_TSL
static void wifi_publish_legacy_telemetry(void);
#endif
static uint8_t wifi_send_command(const char *cmd, ULONG timeout_ms);
static void wifi_refresh_rssi(void);
static void wifi_schedule_restart(void);
static void wifi_rx_clear(void);
static uint8_t wifi_rx_pop(uint8_t *data);
static void wifi_usart_send_string(const char *str);
static void wifi_reset_retry_state(void);
static void wifi_schedule_retry_after_failure(void);
static uint8_t wifi_retry_ready(void);
static void wifi_check_auto_rule(void);
static void wifi_check_restart(void);
static void wifi_set_default_ap_ssid(void);
static void wifi_update_ap_ssid_from_suffix(const char *suffix);
static uint8_t wifi_refresh_ap_ssid(void);
static size_t wifi_strnlen_local(const char *text, size_t max_len);
static uint32_t wifi_config_checksum(const WIFI_STA_CONFIG *config);
static uint8_t wifi_config_is_valid(const WIFI_STA_CONFIG *config);
static void wifi_load_sta_config(void);
static uint8_t wifi_save_sta_config(const char *ssid, const char *password);
static uint8_t wifi_clear_sta_config(void);
static uint8_t wifi_escape_at_string(const char *source, char *target, size_t target_size);
static uint8_t wifi_start_config_portal(void);
static void wifi_stop_config_portal(void);
static uint8_t wifi_send_tcp_data(uint8_t connection_id, const char *data);
static void wifi_send_http_response(uint8_t connection_id, const char *status, const char *body);
static void wifi_send_config_page(uint8_t connection_id, const char *message);
static void wifi_send_saved_page(uint8_t connection_id, const char *ssid);
static uint8_t wifi_parse_ipd_line(const char *line, uint8_t *connection_id, const char **payload);
static void wifi_url_decode(const char *source, size_t source_len, char *target, size_t target_size);
static uint8_t wifi_get_query_value(const char *query, const char *key, char *value, size_t value_size);
static void wifi_handle_http_request(const char *line);

static ULONG wifi_ms_to_ticks(ULONG ms)
{
    ULONG ticks;

    ticks = (ms * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U;
    return (ticks == 0U) ? 1U : ticks;
}

static ULONG wifi_uptime_seconds(void)
{
    return tx_time_get() / TX_TIMER_TICKS_PER_SECOND;
}

static void wifi_reset_retry_state(void)
{
    wifi_retry_fail_count = 0U;
    wifi_retry_deadline = 0U;
}

static void wifi_schedule_retry_after_failure(void)
{
    ULONG interval_ms;

    if (wifi_sta_config_valid == 0U)
    {
        return;
    }

    if (wifi_retry_fail_count < 255U)
    {
        wifi_retry_fail_count++;
    }

    interval_ms = (wifi_retry_fail_count <= WIFI_RETRY_FAST_ATTEMPTS) ?
                  WIFI_RETRY_FAST_INTERVAL_MS :
                  WIFI_RETRY_SLOW_INTERVAL_MS;

    wifi_request = 1U;
    wifi_retry_deadline = tx_time_get() + wifi_ms_to_ticks(interval_ms);
}

static uint8_t wifi_retry_ready(void)
{
    if (wifi_request == 0U)
    {
        return 0U;
    }

    if (wifi_retry_deadline == 0U)
    {
        return 1U;
    }

    return ((LONG)(tx_time_get() - wifi_retry_deadline) >= 0) ? 1U : 0U;
}

static void wifi_set_default_ap_ssid(void)
{
    strcpy(wifi_ap_ssid, WIFI_AP_SSID_FALLBACK);
}

static void wifi_update_ap_ssid_from_suffix(const char *suffix)
{
    if ((suffix == NULL) || (suffix[0] == '\0'))
    {
        wifi_set_default_ap_ssid();
        return;
    }

    snprintf(wifi_ap_ssid, sizeof(wifi_ap_ssid), "%s%s", WIFI_AP_SSID_PREFIX, suffix);
}

static uint8_t wifi_refresh_ap_ssid(void)
{
    ULONG start;
    uint8_t data;
    char response[WIFI_RX_BUFFER_SIZE];
    uint16_t len = 0U;
    const char *line_start;
    const char *quote_start;
    const char *quote_end;
    char suffix[5];
    uint16_t suffix_len = 0U;
    const char *p;

    wifi_set_default_ap_ssid();
    wifi_rx_clear();
    wifi_usart_send_string("AT+CIPAPMAC?\r\n");

    response[0] = '\0';
    start = tx_time_get();
    while ((ULONG)(tx_time_get() - start) < wifi_ms_to_ticks(2000U))
    {
        if (wifi_rx_pop(&data) == 0U)
        {
            tx_thread_sleep(1U);
            continue;
        }

        if (len < (WIFI_RX_BUFFER_SIZE - 1U))
        {
            response[len++] = (char)data;
            response[len] = '\0';
        }
        else
        {
            memmove(response, response + 1, WIFI_RX_BUFFER_SIZE - 2U);
            response[WIFI_RX_BUFFER_SIZE - 2U] = (char)data;
            response[WIFI_RX_BUFFER_SIZE - 1U] = '\0';
        }

        if ((strstr(response, "\r\nOK\r\n") != NULL) || (strstr(response, "\nOK\r\n") != NULL))
        {
            break;
        }
        if (strstr(response, "ERROR") != NULL)
        {
            return 0U;
        }
    }

    line_start = strstr(response, "+CIPAPMAC:");
    if (line_start == NULL)
    {
        return 0U;
    }

    quote_start = strchr(line_start, '"');
    if (quote_start == NULL)
    {
        return 0U;
    }
    quote_end = strchr(quote_start + 1, '"');
    if ((quote_end == NULL) || (quote_end <= quote_start))
    {
        return 0U;
    }

    for (p = quote_end - 1; p > quote_start; p--)
    {
        if ((*p == ':') || (*p == '"'))
        {
            continue;
        }

        suffix[3U - suffix_len] = *p;
        suffix_len++;
        if (suffix_len >= 4U)
        {
            break;
        }
    }

    if (suffix_len != 4U)
    {
        return 0U;
    }

    suffix[4] = '\0';
    wifi_update_ap_ssid_from_suffix(suffix);
    return 1U;
}

static size_t wifi_strnlen_local(const char *text, size_t max_len)
{
    size_t len = 0U;

    if (text == NULL)
    {
        return 0U;
    }

    while ((len < max_len) && (text[len] != '\0'))
    {
        len++;
    }

    return len;
}

static uint32_t wifi_config_checksum(const WIFI_STA_CONFIG *config)
{
    const uint8_t *bytes;
    uint32_t hash = 5381UL;
    size_t index;
    size_t total_length;

    if (config == NULL)
    {
        return 0UL;
    }

    bytes = (const uint8_t *)config->ssid;
    total_length = sizeof(config->ssid) + sizeof(config->password);
    for (index = 0U; index < total_length; index++)
    {
        hash = ((hash << 5) + hash) + bytes[index];
    }

    hash ^= config->magic;
    hash ^= config->version;
    return hash;
}

static uint8_t wifi_config_is_valid(const WIFI_STA_CONFIG *config)
{
    size_t ssid_len;
    size_t password_len;

    if (config == NULL)
    {
        return 0U;
    }

    if ((config->magic != WIFI_CONFIG_MAGIC) || (config->version != WIFI_CONFIG_VERSION))
    {
        return 0U;
    }

    ssid_len = wifi_strnlen_local(config->ssid, sizeof(config->ssid));
    password_len = wifi_strnlen_local(config->password, sizeof(config->password));
    if ((ssid_len == 0U) || (ssid_len >= sizeof(config->ssid)) || (password_len >= sizeof(config->password)))
    {
        return 0U;
    }

    if (config->checksum != wifi_config_checksum(config))
    {
        return 0U;
    }

    return 1U;
}

static void wifi_load_sta_config(void)
{
    const WIFI_STA_CONFIG *flash_config;

    flash_config = (const WIFI_STA_CONFIG *)WIFI_CONFIG_FLASH_ADDRESS;
    if (wifi_config_is_valid(flash_config) != 0U)
    {
        memcpy(&wifi_sta_config, flash_config, sizeof(wifi_sta_config));
        wifi_sta_config_valid = 1U;
    }
    else
    {
        memset(&wifi_sta_config, 0, sizeof(wifi_sta_config));
        wifi_sta_config_valid = 0U;
    }
}

static uint8_t wifi_save_sta_config(const char *ssid, const char *password)
{
    WIFI_STA_CONFIG new_config;
    const uint32_t *words;
    uint32_t address;
    size_t index;
    size_t ssid_len;
    size_t password_len;
    FLASH_Status flash_status;

    if ((ssid == NULL) || (password == NULL))
    {
        return 0U;
    }

    ssid_len = strlen(ssid);
    password_len = strlen(password);
    if ((ssid_len == 0U) ||
        (ssid_len > WIFI_STA_SSID_MAX_LEN) ||
        (password_len > WIFI_STA_PASSWORD_MAX_LEN))
    {
        return 0U;
    }

    memset(&new_config, 0, sizeof(new_config));
    new_config.magic = WIFI_CONFIG_MAGIC;
    new_config.version = WIFI_CONFIG_VERSION;
    memcpy(new_config.ssid, ssid, ssid_len);
    memcpy(new_config.password, password, password_len);
    new_config.checksum = wifi_config_checksum(&new_config);

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    flash_status = FLASH_EraseSector(WIFI_CONFIG_FLASH_SECTOR, WIFI_CONFIG_FLASH_VOLTAGE_RANGE);
    if (flash_status != FLASH_COMPLETE)
    {
        FLASH_Lock();
        return 0U;
    }

    words = (const uint32_t *)&new_config;
    address = WIFI_CONFIG_FLASH_ADDRESS;
    for (index = 0U; index < (sizeof(new_config) / sizeof(uint32_t)); index++)
    {
        flash_status = FLASH_ProgramWord(address, words[index]);
        if (flash_status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return 0U;
        }
        address += sizeof(uint32_t);
    }

    FLASH_Lock();
    memcpy(&wifi_sta_config, &new_config, sizeof(wifi_sta_config));
    wifi_sta_config_valid = 1U;
    return 1U;
}

static uint8_t wifi_clear_sta_config(void)
{
    FLASH_Status flash_status;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    flash_status = FLASH_EraseSector(WIFI_CONFIG_FLASH_SECTOR, WIFI_CONFIG_FLASH_VOLTAGE_RANGE);
    FLASH_Lock();
    if (flash_status != FLASH_COMPLETE)
    {
        return 0U;
    }

    memset(&wifi_sta_config, 0, sizeof(wifi_sta_config));
    wifi_sta_config_valid = 0U;
    return 1U;
}

static ULONG wifi_timer_remain_seconds(void)
{
    ULONG now;

    if (timer_enable == 0U)
    {
        return 0U;
    }

    now = tx_time_get();
    if ((LONG)(timer_deadline - now) <= 0)
    {
        return 0U;
    }

    return (timer_deadline - now) / TX_TIMER_TICKS_PER_SECOND;
}

static uint8_t wifi_wait_module_ready(void)
{
    unsigned int retry;

    for (retry = 0U; retry < 5U; retry++)
    {
        if (wifi_send_command("AT\r\n", 1000U) != 0U)
        {
            return 1U;
        }
        tx_thread_sleep(wifi_ms_to_ticks(300U));
    }

    return 0U;
}

static uint8_t wifi_escape_at_string(const char *source, char *target, size_t target_size)
{
    size_t source_index = 0U;
    size_t target_index = 0U;
    char ch;

    if ((source == NULL) || (target == NULL) || (target_size == 0U))
    {
        return 0U;
    }

    while ((ch = source[source_index]) != '\0')
    {
        if ((ch == '"') || (ch == '\\'))
        {
            if ((target_index + 2U) >= target_size)
            {
                return 0U;
            }
            target[target_index++] = '\\';
            target[target_index++] = ch;
        }
        else
        {
            if (((unsigned char)ch < 0x20U) || ((target_index + 1U) >= target_size))
            {
                return 0U;
            }
            target[target_index++] = ch;
        }
        source_index++;
    }

    target[target_index] = '\0';
    return 1U;
}

static uint8_t wifi_start_config_portal(void)
{
    char cmd[WIFI_CMD_BUFFER_SIZE];

    wifi_reset_retry_state();
    wifi_state = WIFI_STATE_ING;
    wifi_step = WIFI_WIFI_STEP_AT;
    mqtt_state = WIFI_STATE_IDLE;
    mqtt_step = WIFI_MQTT_STEP_NONE;
    publish_enabled = 0U;
    publish_request = 0U;
    last_rssi_update_tick = 0U;

    if (wifi_wait_module_ready() == 0U)
    {
        wifi_state = WIFI_STATE_FAIL;
        return 0U;
    }

    wifi_send_command("ATE0\r\n", 1000U);
    wifi_send_command("AT+MQTTCLEAN=0\r\n", 1000U);
    wifi_send_command("AT+CIPSERVER=0\r\n", 1000U);
    wifi_send_command("AT+CIPMUX=0\r\n", 1000U);

    wifi_step = WIFI_WIFI_STEP_MODE;
    sprintf(cmd, "AT+CWMODE=%u\r\n", WIFI_AP_MODE);
    if (wifi_send_command(cmd, 1000U) == 0U)
    {
        wifi_state = WIFI_STATE_FAIL;
        return 0U;
    }

    wifi_step = WIFI_WIFI_STEP_AP;
    (void)wifi_refresh_ap_ssid();
    sprintf(cmd,
            "AT+CWSAP=\"%s\",\"%s\",%u,%u\r\n",
            wifi_ap_ssid,
            WIFI_AP_PASSWORD,
            WIFI_AP_CHANNEL,
            WIFI_AP_ENCRYPTION);
    if (wifi_send_command(cmd, 3000U) == 0U)
    {
        wifi_state = WIFI_STATE_FAIL;
        return 0U;
    }

    if (wifi_send_command("AT+CIPMUX=1\r\n", 1000U) == 0U)
    {
        wifi_state = WIFI_STATE_FAIL;
        return 0U;
    }

    if (wifi_send_command("AT+CIPSERVER=1,80\r\n", 2000U) == 0U)
    {
        wifi_state = WIFI_STATE_FAIL;
        return 0U;
    }

    wifi_send_command("AT+CIPSTO=60\r\n", 1000U);
    wifi_portal_active = 1U;
    wifi_state = WIFI_STATE_AP;
    wifi_step = WIFI_WIFI_STEP_AP;
    return 1U;
}

static void wifi_stop_config_portal(void)
{
    wifi_send_command("AT+CIPSERVER=0\r\n", 1000U);
    wifi_send_command("AT+CIPMUX=0\r\n", 1000U);
    wifi_portal_active = 0U;
}

static void wifi_load_set(uint8_t on)
{
    switch_state = (on != 0U) ? 1U : 0U;
    if (switch_state == 0U)
    {
        sensor_temperature = 0U;
        sensor_humidity = 0U;
        sensor_valid = 0U;
    }
}

static void wifi_gpio_init(void)
{
    wifi_load_set(WIFI_SWITCH_DEFAULT_ON);
}

static void wifi_usart_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &gpio);

    USART_DeInit(USART3);
    USART_StructInit(&usart);
    usart.USART_BaudRate = 115200U;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &usart);

    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    nvic.NVIC_IRQChannel = USART3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 6U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(USART3, ENABLE);
}

static void wifi_rx_clear(void)
{
    wifi_rx_head = 0U;
    wifi_rx_tail = 0U;
}

static void wifi_rx_push(uint8_t data)
{
    uint16_t next;

    next = (uint16_t)((wifi_rx_head + 1U) % WIFI_RX_BUFFER_SIZE);
    if (next != wifi_rx_tail)
    {
        wifi_rx_buffer[wifi_rx_head] = data;
        wifi_rx_head = next;
    }
}

static uint8_t wifi_rx_pop(uint8_t *data)
{
    if (wifi_rx_head == wifi_rx_tail)
    {
        return 0U;
    }

    *data = wifi_rx_buffer[wifi_rx_tail];
    wifi_rx_tail = (uint16_t)((wifi_rx_tail + 1U) % WIFI_RX_BUFFER_SIZE);
    return 1U;
}

static void wifi_usart_send_char(char data)
{
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
    {
    }
    USART_SendData(USART3, (uint16_t)data);
}

static void wifi_usart_send_string(const char *str)
{
    while (*str != '\0')
    {
        wifi_usart_send_char(*str++);
    }
}

static uint8_t wifi_wait_char(char expected, ULONG timeout_ms)
{
    ULONG start;
    uint8_t data;

    start = tx_time_get();
    while ((ULONG)(tx_time_get() - start) < wifi_ms_to_ticks(timeout_ms))
    {
        if (wifi_rx_pop(&data) == 0U)
        {
            tx_thread_sleep(1U);
            continue;
        }

        if ((char)data == expected)
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t wifi_wait_response(const char *expected, ULONG timeout_ms)
{
    ULONG start;
    uint8_t data;
    char response[WIFI_RX_BUFFER_SIZE];
    uint16_t len = 0U;

    response[0] = '\0';
    start = tx_time_get();
    while ((ULONG)(tx_time_get() - start) < wifi_ms_to_ticks(timeout_ms))
    {
        if (wifi_rx_pop(&data) == 0U)
        {
            tx_thread_sleep(1U);
            continue;
        }

        if (len < (WIFI_RX_BUFFER_SIZE - 1U))
        {
            response[len++] = (char)data;
            response[len] = '\0';
            if (strstr(response, expected) != NULL)
            {
                return 1U;
            }
        }
        else
        {
            memmove(response, response + 1, WIFI_RX_BUFFER_SIZE - 2U);
            response[WIFI_RX_BUFFER_SIZE - 2U] = (char)data;
            response[WIFI_RX_BUFFER_SIZE - 1U] = '\0';
        }
    }

    return 0U;
}

static uint8_t wifi_send_command2(const char *cmd, const char *expected, ULONG timeout_ms)
{
    wifi_rx_clear();
    wifi_usart_send_string(cmd);
    return wifi_wait_response(expected, timeout_ms);
}

static uint8_t wifi_send_command(const char *cmd, ULONG timeout_ms)
{
    return wifi_send_command2(cmd, "OK", timeout_ms);
}

static int wifi_parse_cwjap_rssi(const char *line, int *rssi)
{
    const char *p;
    const char *field_start = NULL;
    int field_index = 0;
    int in_quotes = 0;
    char number[16];
    uint16_t len;

    if ((line == NULL) || (rssi == NULL))
    {
        return 0;
    }

    p = strchr(line, ':');
    if (p == NULL)
    {
        return 0;
    }
    p++;
    field_start = p;

    while (*p != '\0')
    {
        if (*p == '"')
        {
            in_quotes = !in_quotes;
        }
        else if ((*p == ',') && (in_quotes == 0))
        {
            if (field_index == 3)
            {
                break;
            }
            field_index++;
            field_start = p + 1;
        }
        p++;
    }

    if (field_index != 3)
    {
        return 0;
    }

    len = (uint16_t)(p - field_start);
    if ((len == 0U) || (len >= sizeof(number)))
    {
        return 0;
    }

    memcpy(number, field_start, len);
    number[len] = '\0';
    *rssi = atoi(number);
    return 1;
}

static void wifi_refresh_rssi(void)
{
    ULONG now;
    ULONG start;
    uint8_t data;
    char response[WIFI_RX_BUFFER_SIZE];
    uint16_t len = 0U;
    char *line_start;
    char *line_end;
    int parsed_rssi;

    if (wifi_state != WIFI_STATE_OK)
    {
        return;
    }

    now = tx_time_get();
    if ((last_rssi_update_tick != 0U) &&
        ((ULONG)(now - last_rssi_update_tick) < wifi_ms_to_ticks(WIFI_RSSI_REFRESH_PERIOD_S * 1000U)))
    {
        return;
    }

    wifi_rx_clear();
    wifi_usart_send_string("AT+CWJAP?\r\n");

    response[0] = '\0';
    start = tx_time_get();
    while ((ULONG)(tx_time_get() - start) < wifi_ms_to_ticks(2000U))
    {
        if (wifi_rx_pop(&data) == 0U)
        {
            tx_thread_sleep(1U);
            continue;
        }

        if (len < (WIFI_RX_BUFFER_SIZE - 1U))
        {
            response[len++] = (char)data;
            response[len] = '\0';
        }

        if ((strstr(response, "\r\nOK\r\n") != NULL) || (strstr(response, "\nOK\r\n") != NULL))
        {
            break;
        }
        if (strstr(response, "ERROR") != NULL)
        {
            return;
        }
    }

    line_start = strstr(response, "+CWJAP:");
    if (line_start == NULL)
    {
        return;
    }

    line_end = strchr(line_start, '\r');
    if (line_end == NULL)
    {
        line_end = strchr(line_start, '\n');
    }
    if (line_end != NULL)
    {
        *line_end = '\0';
    }

    if (wifi_parse_cwjap_rssi(line_start, &parsed_rssi) != 0)
    {
        wifi_rssi = parsed_rssi;
        last_rssi_update_tick = tx_time_get();
    }
}

static uint8_t wifi_connect_wifi(void)
{
    char cmd[WIFI_CMD_BUFFER_SIZE];
    char escaped_ssid[(WIFI_STA_SSID_MAX_LEN * 2U) + 1U];
    char escaped_password[(WIFI_STA_PASSWORD_MAX_LEN * 2U) + 1U];

    if (wifi_sta_config_valid == 0U)
    {
        return wifi_start_config_portal();
    }

    wifi_state = WIFI_STATE_ING;
    wifi_step = WIFI_WIFI_STEP_AT;
    if (wifi_wait_module_ready() == 0U)
    {
        wifi_state = WIFI_STATE_FAIL;
        return 0U;
    }

    wifi_send_command("ATE0\r\n", 1000U);
    wifi_send_command("AT+MQTTCLEAN=0\r\n", 1000U);
    wifi_stop_config_portal();

    wifi_step = WIFI_WIFI_STEP_MODE;
    sprintf(cmd, "AT+CWMODE=%u\r\n", WIFI_STA_MODE);
    if (wifi_send_command(cmd, 1000U) == 0U)
    {
        wifi_state = WIFI_STATE_FAIL;
        return 0U;
    }

    if ((wifi_escape_at_string(wifi_sta_config.ssid, escaped_ssid, sizeof(escaped_ssid)) == 0U) ||
        (wifi_escape_at_string(wifi_sta_config.password, escaped_password, sizeof(escaped_password)) == 0U))
    {
        wifi_state = WIFI_STATE_FAIL;
        return 0U;
    }

    wifi_step = WIFI_WIFI_STEP_JOIN;
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", escaped_ssid, escaped_password);
    if (wifi_send_command(cmd, 15000U) == 0U)
    {
        wifi_state = WIFI_STATE_FAIL;
        return 0U;
    }

    wifi_step = WIFI_WIFI_STEP_NONE;
    wifi_state = WIFI_STATE_OK;
    wifi_reset_retry_state();
    return 1U;
}

static uint8_t wifi_config_user(void)
{
    char cmd[WIFI_CMD_BUFFER_SIZE];

    wifi_send_command("AT+MQTTCLEAN=0\r\n", 1000U);
    sprintf(cmd,
            "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
            clientId,
            username,
            passwd);
    return wifi_send_command(cmd, 2000U);
}

static uint8_t wifi_connect_host(void)
{
    char cmd[WIFI_CMD_BUFFER_SIZE];

    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",%s,0\r\n", mqttHostUrl, serverPort);
    return wifi_send_command(cmd, 5000U);
}

static uint8_t wifi_mqtt_subscribe(const char *topic, unsigned int qos)
{
    char cmd[WIFI_CMD_BUFFER_SIZE];

    sprintf(cmd, "AT+MQTTSUB=0,\"%s\",%u\r\n", topic, qos);
    return wifi_send_command(cmd, 3000U);
}

static uint8_t wifi_connect_broker(void)
{
    uint8_t subscribe_ok = 0U;

    if (wifi_state != WIFI_STATE_OK)
    {
        if (wifi_connect_wifi() == 0U)
        {
            mqtt_state = (wifi_portal_active != 0U) ? WIFI_STATE_IDLE : WIFI_STATE_FAIL;
            return 0U;
        }
    }

    mqtt_state = WIFI_STATE_ING;
    mqtt_step = WIFI_MQTT_STEP_CFG;
    if (wifi_config_user() == 0U)
    {
        mqtt_state = WIFI_STATE_FAIL;
        return 0U;
    }

    mqtt_step = WIFI_MQTT_STEP_CONN;
    if (wifi_connect_host() == 0U)
    {
        mqtt_state = WIFI_STATE_FAIL;
        return 0U;
    }

    mqtt_step = WIFI_MQTT_STEP_SUB;
#if WIFI_ENABLE_LEGACY_ALIYUN_TSL
    if (wifi_mqtt_subscribe(legacySubscribeTopic, WIFI_MQTT_QOS1) != 0U)
    {
        subscribe_ok = 1U;
    }
#endif
#if WIFI_ENABLE_CUSTOM_CMD_TOPIC
    if (wifi_mqtt_subscribe(cmdTopic, WIFI_MQTT_QOS1) != 0U)
    {
        subscribe_ok = 1U;
    }
#endif
    if (subscribe_ok == 0U)
    {
#if !WIFI_ENABLE_LEGACY_ALIYUN_TSL && !WIFI_ENABLE_CUSTOM_CMD_TOPIC
        subscribe_ok = 1U;
#else
        mqtt_state = WIFI_STATE_FAIL;
        return 0U;
#endif
    }

    mqtt_step = WIFI_MQTT_STEP_NONE;
    mqtt_state = WIFI_STATE_OK;
    publish_request = 1U;
    wifi_publish_status(1U, "connected");
    wifi_publish_event("mqtt_reconnect", "mqtt connected");
    return 1U;
}

static uint8_t wifi_publish_raw(const char *topic, const char *payload, unsigned int qos)
{
    char cmd[WIFI_CMD_BUFFER_SIZE];

    if (mqtt_state != WIFI_STATE_OK)
    {
        return 0U;
    }

    sprintf(cmd, "AT+MQTTPUBRAW=0,\"%s\",%u,%u,0\r\n", topic, (unsigned int)strlen(payload), qos);
    wifi_rx_clear();
    wifi_usart_send_string(cmd);
    if (wifi_wait_char('>', 3000U) == 0U)
    {
        return 0U;
    }

    wifi_usart_send_string(payload);
    return wifi_wait_response("OK", 5000U);
}

static uint8_t wifi_send_tcp_data(uint8_t connection_id, const char *data)
{
    char cmd[32];

    if (data == NULL)
    {
        return 0U;
    }

    sprintf(cmd, "AT+CIPSEND=%u,%u\r\n", connection_id, (unsigned int)strlen(data));
    wifi_rx_clear();
    wifi_usart_send_string(cmd);
    if (wifi_wait_char('>', 3000U) == 0U)
    {
        return 0U;
    }

    wifi_usart_send_string(data);
    return wifi_wait_response("SEND OK", 5000U);
}

static void wifi_send_http_response(uint8_t connection_id, const char *status, const char *body)
{
    char response[WIFI_HTTP_BUFFER_SIZE];
    char close_cmd[24];
    int length;

    if (body == NULL)
    {
        body = "";
    }

    length = snprintf(response,
                      sizeof(response),
                      "HTTP/1.1 %s\r\n"
                      "Content-Type: text/html; charset=utf-8\r\n"
                      "Connection: close\r\n"
                      "Cache-Control: no-store\r\n"
                      "Content-Length: %u\r\n"
                      "\r\n"
                      "%s",
                      (status != NULL) ? status : "200 OK",
                      (unsigned int)strlen(body),
                      body);
    if ((length <= 0) || ((size_t)length >= sizeof(response)))
    {
        strcpy(response,
               "HTTP/1.1 500 Internal Server Error\r\n"
               "Content-Type: text/plain\r\n"
               "Connection: close\r\n"
               "Content-Length: 5\r\n"
               "\r\n"
               "error");
    }

    (void)wifi_send_tcp_data(connection_id, response);
    sprintf(close_cmd, "AT+CIPCLOSE=%u\r\n", connection_id);
    (void)wifi_send_command(close_cmd, 1000U);
}

static void wifi_send_config_page(uint8_t connection_id, const char *message)
{
    char body[WIFI_HTTP_BUFFER_SIZE];
    const char *status_text;

    status_text = (wifi_sta_config_valid != 0U) ? "saved" : "not saved";
    snprintf(body,
             sizeof(body),
             "<!doctype html>"
             "<html><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>Wi-Fi Setup</title>"
             "<style>"
             "body{font-family:Arial,sans-serif;margin:24px;background:#f4f7fb;color:#1f2937;}"
             "main{max-width:420px;margin:0 auto;padding:24px;background:#fff;border-radius:12px;box-shadow:0 8px 24px rgba(15,23,42,.08);}"
             "h1{font-size:24px;margin:0 0 12px;}p{line-height:1.5;}label{display:block;margin:14px 0 6px;font-weight:600;}"
             "input{width:100%%;padding:12px;border:1px solid #cbd5e1;border-radius:8px;box-sizing:border-box;}"
             "button{width:100%%;margin-top:18px;padding:12px;border:none;border-radius:8px;background:#2563eb;color:#fff;font-size:16px;}"
             ".msg{padding:10px 12px;background:#e0f2fe;border-radius:8px;color:#075985;}"
             ".hint{font-size:13px;color:#64748b;}"
             "</style></head><body><main>"
             "<h1>Device Wi-Fi Setup</h1>"
             "<p class=\"msg\">%s</p>"
             "<p class=\"hint\">Saved Wi-Fi: %s. MQTT settings stay fixed in firmware.</p>"
             "<form action=\"/set\" method=\"get\">"
             "<label for=\"ssid\">Wi-Fi Name</label>"
             "<input id=\"ssid\" name=\"ssid\" maxlength=\"32\" placeholder=\"Enter router SSID\">"
             "<label for=\"pwd\">Wi-Fi Password</label>"
             "<input id=\"pwd\" name=\"pwd\" type=\"password\" maxlength=\"64\" placeholder=\"Enter router password\">"
             "<button type=\"submit\">Save And Connect</button>"
             "</form></main></body></html>",
             (message != NULL) ? message : "Enter the Wi-Fi that the device should connect to.",
             status_text);
    wifi_send_http_response(connection_id, "200 OK", body);
}

static void wifi_send_saved_page(uint8_t connection_id, const char *ssid)
{
    char body[WIFI_HTTP_BUFFER_SIZE];

    (void)ssid;

    snprintf(body,
             sizeof(body),
             "<!doctype html>"
             "<html><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>Saved</title>"
             "<style>"
             "body{font-family:Arial,sans-serif;margin:24px;background:#f4f7fb;color:#1f2937;}"
             "main{max-width:420px;margin:0 auto;padding:24px;background:#fff;border-radius:12px;box-shadow:0 8px 24px rgba(15,23,42,.08);}"
             "h1{font-size:24px;margin:0 0 12px;}p{line-height:1.6;}"
             "</style></head><body><main>"
             "<h1>Configuration Saved</h1>"
             "<p>The device stored the new Wi-Fi credentials successfully.</p>"
             "<p>It will restart in a moment, leave AP mode, and then connect to your router automatically.</p>"
             "</main></body></html>");
    wifi_send_http_response(connection_id, "200 OK", body);
}

static uint8_t wifi_parse_ipd_line(const char *line, uint8_t *connection_id, const char **payload)
{
    const char *start;
    const char *colon;
    char *end = NULL;
    long parsed_id;

    if ((line == NULL) || (connection_id == NULL) || (payload == NULL))
    {
        return 0U;
    }

    start = strstr(line, "+IPD,");
    if (start == NULL)
    {
        return 0U;
    }

    parsed_id = strtol(start + 5, &end, 10);
    if ((end == NULL) || (*end != ',') || (parsed_id < 0L) || (parsed_id > 4L))
    {
        return 0U;
    }

    colon = strchr(end + 1, ':');
    if (colon == NULL)
    {
        return 0U;
    }

    *connection_id = (uint8_t)parsed_id;
    *payload = colon + 1;
    return 1U;
}

static int wifi_hex_value(char ch)
{
    if ((ch >= '0') && (ch <= '9'))
    {
        return ch - '0';
    }
    if ((ch >= 'a') && (ch <= 'f'))
    {
        return ch - 'a' + 10;
    }
    if ((ch >= 'A') && (ch <= 'F'))
    {
        return ch - 'A' + 10;
    }
    return -1;
}

static void wifi_url_decode(const char *source, size_t source_len, char *target, size_t target_size)
{
    size_t source_index = 0U;
    size_t target_index = 0U;
    int high;
    int low;

    if ((source == NULL) || (target == NULL) || (target_size == 0U))
    {
        return;
    }

    while ((source_index < source_len) && (target_index + 1U < target_size))
    {
        if ((source[source_index] == '%') && ((source_index + 2U) < source_len))
        {
            high = wifi_hex_value(source[source_index + 1U]);
            low = wifi_hex_value(source[source_index + 2U]);
            if ((high >= 0) && (low >= 0))
            {
                target[target_index++] = (char)((high << 4) | low);
                source_index += 3U;
                continue;
            }
        }

        if (source[source_index] == '+')
        {
            target[target_index++] = ' ';
        }
        else
        {
            target[target_index++] = source[source_index];
        }
        source_index++;
    }

    target[target_index] = '\0';
}

static uint8_t wifi_get_query_value(const char *query, const char *key, char *value, size_t value_size)
{
    const char *cursor;
    size_t key_len;
    const char *pair_end;

    if ((query == NULL) || (key == NULL) || (value == NULL) || (value_size == 0U))
    {
        return 0U;
    }

    key_len = strlen(key);
    cursor = query;
    while (*cursor != '\0')
    {
        pair_end = strchr(cursor, '&');
        if (pair_end == NULL)
        {
            pair_end = cursor + strlen(cursor);
        }

        if ((pair_end > cursor) &&
            ((size_t)(pair_end - cursor) > key_len) &&
            (strncmp(cursor, key, key_len) == 0) &&
            (cursor[key_len] == '='))
        {
            wifi_url_decode(cursor + key_len + 1U,
                            (size_t)(pair_end - cursor - key_len - 1U),
                            value,
                            value_size);
            return 1U;
        }

        if (*pair_end == '\0')
        {
            break;
        }
        cursor = pair_end + 1;
    }

    value[0] = '\0';
    return 0U;
}

static void wifi_handle_http_request(const char *line)
{
    uint8_t connection_id;
    const char *payload;
    const char *path_start;
    const char *path_end;
    char path[WIFI_LINE_BUFFER_SIZE];
    char ssid[WIFI_STA_SSID_MAX_LEN + 1U];
    char password[WIFI_STA_PASSWORD_MAX_LEN + 1U];

    if (wifi_parse_ipd_line(line, &connection_id, &payload) == 0U)
    {
        return;
    }

    if (strncmp(payload, "GET ", 4) != 0)
    {
        wifi_send_config_page(connection_id, "Only GET requests are supported.");
        return;
    }

    path_start = payload + 4;
    path_end = strchr(path_start, ' ');
    if (path_end == NULL)
    {
        wifi_send_config_page(connection_id, "Invalid request line.");
        return;
    }

    if ((size_t)(path_end - path_start) >= sizeof(path))
    {
        wifi_send_config_page(connection_id, "Request path is too long.");
        return;
    }

    memcpy(path, path_start, (size_t)(path_end - path_start));
    path[path_end - path_start] = '\0';

    if (strncmp(path, "/set?", 5) == 0)
    {
        if ((wifi_get_query_value(path + 5, "ssid", ssid, sizeof(ssid)) == 0U) || (ssid[0] == '\0'))
        {
            wifi_send_config_page(connection_id, "Please enter a Wi-Fi name.");
            return;
        }

        (void)wifi_get_query_value(path + 5, "pwd", password, sizeof(password));
        if (wifi_save_sta_config(ssid, password) == 0U)
        {
            wifi_send_config_page(connection_id, "Saving failed. Please try again.");
            return;
        }

        wifi_send_saved_page(connection_id, ssid);
        wifi_schedule_restart();
        return;
    }

    wifi_send_config_page(connection_id, "Connect this device to your router by submitting the form below.");
}

static const char *wifi_json_get_string(cJSON *root, const char *name)
{
    cJSON *item;

    item = cJSON_GetObjectItem(root, name);
    if ((item != NULL) && cJSON_IsString(item) && (item->valuestring != NULL))
    {
        return item->valuestring;
    }

    return NULL;
}

static uint8_t wifi_string_equals_any(const char *value, const char *const *choices, unsigned int count)
{
    unsigned int index;

    if ((value == NULL) || (choices == NULL))
    {
        return 0U;
    }

    for (index = 0U; index < count; index++)
    {
        if (strcmp(value, choices[index]) == 0)
        {
            return 1U;
        }
    }

    return 0U;
}

static const char *wifi_get_command_text(cJSON *root)
{
    const char *cmd;

    if (root == NULL)
    {
        return NULL;
    }

    cmd = wifi_json_get_string(root, "cmd");
    if (cmd == NULL)
    {
        cmd = wifi_json_get_string(root, "command");
    }
    if (cmd == NULL)
    {
        cmd = wifi_json_get_string(root, "action_type");
    }
    if (cmd == NULL)
    {
        cmd = wifi_json_get_string(root, "type");
    }

    return cmd;
}

static const char *wifi_get_req_id_text(cJSON *root)
{
    const char *req_id;

    if (root == NULL)
    {
        return NULL;
    }

    req_id = wifi_json_get_string(root, "req_id");
    if (req_id == NULL)
    {
        req_id = wifi_json_get_string(root, "request_id");
    }

    return req_id;
}

static cJSON *wifi_get_object_item_any(cJSON *object, const char *const *names, unsigned int count)
{
    unsigned int index;
    cJSON *item;

    if ((object == NULL) || (names == NULL))
    {
        return NULL;
    }

    for (index = 0U; index < count; index++)
    {
        item = cJSON_GetObjectItem(object, names[index]);
        if (item != NULL)
        {
            return item;
        }
    }

    return NULL;
}

static const char *wifi_timer_action_text(void)
{
    return (timer_target_switch != 0U) ? "on" : "off";
}

static uint8_t wifi_json_item_to_switch(cJSON *item, uint8_t *value)
{
    if ((item == NULL) || (value == NULL))
    {
        return 0U;
    }

    if (cJSON_IsBool(item))
    {
        *value = cJSON_IsTrue(item) ? 1U : 0U;
        return 1U;
    }

    if (cJSON_IsNumber(item))
    {
        *value = (item->valueint != 0) ? 1U : 0U;
        return 1U;
    }

    if (cJSON_IsString(item) && (item->valuestring != NULL))
    {
        if ((strcmp(item->valuestring, "on") == 0) ||
            (strcmp(item->valuestring, "1") == 0) ||
            (strcmp(item->valuestring, "open") == 0) ||
            (strcmp(item->valuestring, "enable") == 0) ||
            (strcmp(item->valuestring, "true") == 0))
        {
            *value = 1U;
            return 1U;
        }
        if ((strcmp(item->valuestring, "off") == 0) ||
            (strcmp(item->valuestring, "0") == 0) ||
            (strcmp(item->valuestring, "close") == 0) ||
            (strcmp(item->valuestring, "disable") == 0) ||
            (strcmp(item->valuestring, "false") == 0))
        {
            *value = 0U;
            return 1U;
        }
    }

    return 0U;
}

static uint8_t wifi_json_item_to_int_range(cJSON *item, int min_value, int max_value, int *value)
{
    long parsed_value;
    char *end = NULL;

    if ((item == NULL) || (value == NULL))
    {
        return 0U;
    }

    if (cJSON_IsNumber(item))
    {
        parsed_value = item->valueint;
    }
    else if (cJSON_IsString(item) && (item->valuestring != NULL))
    {
        parsed_value = strtol(item->valuestring, &end, 10);
        if ((end == NULL) || (*end != '\0'))
        {
            return 0U;
        }
    }
    else
    {
        return 0U;
    }

    if ((parsed_value < (long)min_value) || (parsed_value > (long)max_value))
    {
        return 0U;
    }

    *value = (int)parsed_value;
    return 1U;
}

static uint8_t wifi_json_item_to_action(cJSON *item, uint8_t *value)
{
    if ((item == NULL) || (value == NULL))
    {
        return 0U;
    }

    if (cJSON_IsString(item) && (item->valuestring != NULL))
    {
        if (wifi_string_equals_any(item->valuestring,
                                   wifi_action_on_aliases,
                                   sizeof(wifi_action_on_aliases) / sizeof(wifi_action_on_aliases[0])) != 0U)
        {
            *value = 1U;
            return 1U;
        }

        if (wifi_string_equals_any(item->valuestring,
                                   wifi_action_off_aliases,
                                   sizeof(wifi_action_off_aliases) / sizeof(wifi_action_off_aliases[0])) != 0U)
        {
            *value = 0U;
            return 1U;
        }
    }

    return wifi_json_item_to_switch(item, value);
}

static cJSON *wifi_create_message(const char *msg_type, const char *req_id, cJSON **data_out, unsigned int *seq_out)
{
    cJSON *root;
    cJSON *data;
    cJSON *ext;
    unsigned int local_seq;

    local_seq = ++seq;
    root = cJSON_CreateObject();
    if (root == NULL)
    {
        return NULL;
    }

    cJSON_AddStringToObject(root, "proto_ver", WIFI_PROTOCOL_VERSION);
    cJSON_AddStringToObject(root, "product_id", APP_PRODUCT_ID);
    cJSON_AddStringToObject(root, "device_id", APP_DEVICE_ID);
    cJSON_AddStringToObject(root, "msg_type", msg_type);
    if ((req_id != NULL) && (req_id[0] != '\0'))
    {
        cJSON_AddStringToObject(root, "req_id", req_id);
    }
    cJSON_AddNumberToObject(root, "seq", local_seq);
    cJSON_AddNumberToObject(root, "timestamp", wifi_uptime_seconds());
    cJSON_AddNullToObject(root, "error");

    data = cJSON_AddObjectToObject(root, "data");
    ext = cJSON_AddObjectToObject(root, "ext");
    if ((data == NULL) || (ext == NULL))
    {
        cJSON_Delete(root);
        return NULL;
    }

    if (data_out != NULL)
    {
        *data_out = data;
    }
    if (seq_out != NULL)
    {
        *seq_out = local_seq;
    }

    return root;
}

static uint8_t wifi_publish_json(const char *topic, cJSON *root, unsigned int qos)
{
    char *payload;
    uint8_t ok;

    payload = cJSON_PrintUnformatted(root);
    if (payload == NULL)
    {
        return 0U;
    }

    ok = wifi_publish_raw(topic, payload, qos);
    cJSON_free(payload);
    return ok;
}

static void wifi_fill_runtime_data(cJSON *data, uint8_t include_sensor)
{
    if (include_sensor != 0U)
    {
        cJSON_AddNumberToObject(data, "temperature", sensor_valid != 0U ? sensor_temperature : 0U);
        cJSON_AddNumberToObject(data, "humidity", sensor_valid != 0U ? sensor_humidity : 0U);
        cJSON_AddBoolToObject(data, "sensor_ok", sensor_valid != 0U);
    }

    cJSON_AddNumberToObject(data, "switch", switch_state);
    cJSON_AddBoolToObject(data, "timer_enable", timer_enable != 0U);
    cJSON_AddStringToObject(data, "timer_action", wifi_timer_action_text());
    cJSON_AddNumberToObject(data, "timer_remain_s", wifi_timer_remain_seconds());
    cJSON_AddNumberToObject(data, "report_period_s", report_period_seconds);
    cJSON_AddNumberToObject(data, "temp_high_limit", temp_high_limit);
    cJSON_AddNumberToObject(data, "humidity_high_limit", humidity_high_limit);
    cJSON_AddBoolToObject(data, "auto_rule_enable", auto_rule_enable != 0U);
    cJSON_AddBoolToObject(data, "auto_rule_active", auto_rule_active != 0U);
    cJSON_AddNumberToObject(data, "uptime_s", wifi_uptime_seconds());
}

static void wifi_fill_telemetry_data(cJSON *data)
{
    cJSON_AddNumberToObject(data, "report_period_s", report_period_seconds);
    cJSON_AddBoolToObject(data, "sensor_ok", sensor_valid != 0U);
    cJSON_AddBoolToObject(data, "timer_enable", timer_enable != 0U);
    cJSON_AddNumberToObject(data, "timer_remain_s", wifi_timer_remain_seconds());
    cJSON_AddNumberToObject(data, "rssi", wifi_rssi);
    cJSON_AddStringToObject(data, "timer_action", wifi_timer_action_text());
    cJSON_AddNumberToObject(data, "temperature", sensor_valid != 0U ? sensor_temperature : 0U);
    cJSON_AddNumberToObject(data, "humidity", sensor_valid != 0U ? sensor_humidity : 0U);
    cJSON_AddNumberToObject(data, "switch", switch_state);
    cJSON_AddNumberToObject(data, "temp_high_limit", temp_high_limit);
    cJSON_AddNumberToObject(data, "humidity_high_limit", humidity_high_limit);
    cJSON_AddBoolToObject(data, "auto_rule_enable", auto_rule_enable != 0U);
    cJSON_AddBoolToObject(data, "auto_rule_active", auto_rule_active != 0U);
}

#if WIFI_ENABLE_LEGACY_ALIYUN_TSL
static void wifi_publish_legacy_telemetry(void)
{
    cJSON *root;
    cJSON *params;
    char *payload;
    char id[12];

    root = cJSON_CreateObject();
    if (root == NULL)
    {
        return;
    }

    params = cJSON_AddObjectToObject(root, "params");
    if (params == NULL)
    {
        cJSON_Delete(root);
        return;
    }

    sprintf(id, "%u", seq);
    cJSON_AddStringToObject(root, "id", id);
    cJSON_AddStringToObject(root, "version", "1.0");
    cJSON_AddStringToObject(root, "method", "thing.event.property.post");
    cJSON_AddNumberToObject(params, ALIYUN_PROP_HUMIDITY, sensor_valid != 0U ? sensor_humidity : 0U);
    cJSON_AddNumberToObject(params, ALIYUN_PROP_TEMPERATURE, sensor_valid != 0U ? sensor_temperature : 0U);
    cJSON_AddBoolToObject(params, ALIYUN_PROP_SWITCH, switch_state != 0U);
    cJSON_AddNumberToObject(params, ALIYUN_PROP_UPTIME, wifi_uptime_seconds());
    cJSON_AddNumberToObject(params, ALIYUN_PROP_TIMER_REMAIN, wifi_timer_remain_seconds());
    cJSON_AddNumberToObject(params, ALIYUN_PROP_REPORT_PERIOD, report_period_seconds);

    payload = cJSON_PrintUnformatted(root);
    if (payload != NULL)
    {
        (void)wifi_publish_raw(legacyPublishTopic, payload, WIFI_MQTT_QOS0);
        cJSON_free(payload);
    }

    cJSON_Delete(root);
}
#endif

static void wifi_publish_telemetry(void)
{
    cJSON *root;
    cJSON *data;

    wifi_refresh_rssi();

    root = wifi_create_message("telemetry", NULL, &data, NULL);
    if (root == NULL)
    {
        return;
    }

    wifi_fill_telemetry_data(data);

    (void)wifi_publish_json(telemetryTopic, root, WIFI_MQTT_QOS0);
#if WIFI_ENABLE_LEGACY_ALIYUN_TSL
    wifi_publish_legacy_telemetry();
#endif
    cJSON_Delete(root);
}

static void wifi_publish_status(uint8_t online, const char *mqtt_text)
{
#if WIFI_ENABLE_CUSTOM_AUX_TOPICS
    cJSON *root;
    cJSON *data;

    root = wifi_create_message("status", NULL, &data, NULL);
    if (root == NULL)
    {
        return;
    }

    cJSON_AddBoolToObject(data, "online", online != 0U);
    cJSON_AddStringToObject(data,
                            "mqtt",
                            (mqtt_text != NULL) ? mqtt_text :
                            ((mqtt_state == WIFI_STATE_OK) ? "connected" : "disconnected"));
    wifi_fill_runtime_data(data, 0U);
    cJSON_AddStringToObject(data, "fw_ver", WIFI_FW_VERSION);

    (void)wifi_publish_json(statusTopic, root, WIFI_MQTT_QOS1);
    cJSON_Delete(root);
#else
    (void)online;
    (void)mqtt_text;
#endif
}

static void wifi_publish_heartbeat(void)
{
#if WIFI_ENABLE_CUSTOM_AUX_TOPICS
    cJSON *root;
    cJSON *data;

    root = wifi_create_message("heartbeat", NULL, &data, NULL);
    if (root == NULL)
    {
        return;
    }

    cJSON_AddNumberToObject(data, "uptime_s", wifi_uptime_seconds());
    cJSON_AddBoolToObject(data, "online", mqtt_state == WIFI_STATE_OK);

    (void)wifi_publish_json(heartbeatTopic, root, WIFI_MQTT_QOS0);
    cJSON_Delete(root);
#endif
}

static void wifi_publish_event(const char *event_type, const char *message)
{
#if WIFI_ENABLE_CUSTOM_AUX_TOPICS
    cJSON *root;
    cJSON *data;

    root = wifi_create_message("event", NULL, &data, NULL);
    if (root == NULL)
    {
        return;
    }

    cJSON_AddStringToObject(data, "event_type", event_type);
    cJSON_AddNumberToObject(data, "switch", switch_state);
    cJSON_AddStringToObject(data, "timer_action", wifi_timer_action_text());
    if ((message != NULL) && (message[0] != '\0'))
    {
        cJSON_AddStringToObject(data, "message", message);
    }

    (void)wifi_publish_json(eventTopic, root, WIFI_MQTT_QOS1);
    cJSON_Delete(root);
#else
    (void)event_type;
    (void)message;
#endif
}

static const char *wifi_error_name(unsigned int code)
{
    switch (code)
    {
    case WIFI_ERROR_JSON_PARSE:
        return "JSON_PARSE_ERROR";
    case WIFI_ERROR_MISSING_FIELD:
        return "MISSING_FIELD";
    case WIFI_ERROR_INVALID_PARAM:
        return "INVALID_PARAM";
    case WIFI_ERROR_UNSUPPORTED_CMD:
        return "UNSUPPORTED_CMD";
    case WIFI_ERROR_SENSOR:
        return "SENSOR_ERROR";
    case WIFI_ERROR_GPIO:
        return "GPIO_ERROR";
    case WIFI_ERROR_NETWORK:
        return "NETWORK_ERROR";
    case WIFI_ERROR_OK:
    default:
        return "OK";
    }
}

static void wifi_set_error_object(cJSON *root, unsigned int code, const char *message)
{
    cJSON *error;

    if ((root == NULL) || (code == WIFI_ERROR_OK))
    {
        return;
    }

    error = cJSON_CreateObject();
    if (error == NULL)
    {
        return;
    }

    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "name", wifi_error_name(code));
    cJSON_AddStringToObject(error,
                            "message",
                            (message != NULL) ? message : wifi_error_name(code));
    cJSON_ReplaceItemInObject(root, "error", error);
}

static void wifi_publish_reply(const char *req_id,
                               const char *cmd,
                               unsigned int error_code,
                               const char *error_text,
                               ULONG delay_s)
{
#if WIFI_ENABLE_CUSTOM_AUX_TOPICS
    cJSON *root;
    cJSON *data;

    root = wifi_create_message("reply", req_id, &data, NULL);
    if (root == NULL)
    {
        return;
    }

    cJSON_AddStringToObject(data, "cmd", (cmd != NULL) ? cmd : "unknown");
    cJSON_AddStringToObject(data, "result", (error_code == WIFI_ERROR_OK) ? "ok" : "failed");
    wifi_fill_runtime_data(data, 0U);
    if (delay_s != 0U)
    {
        cJSON_AddNumberToObject(data, "delay_s", delay_s);
    }
    wifi_set_error_object(root, error_code, error_text);

    (void)wifi_publish_json(replyTopic, root, WIFI_MQTT_QOS1);
    cJSON_Delete(root);
#else
    (void)req_id;
    (void)cmd;
    (void)error_code;
    (void)error_text;
    (void)delay_s;
#endif
}

static cJSON *wifi_get_json_item(cJSON *root, const char *direct_name, const char *param_name)
{
    cJSON *item;
    cJSON *params;

    item = cJSON_GetObjectItem(root, direct_name);
    if (item != NULL)
    {
        return item;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (params == NULL)
    {
        return NULL;
    }

    return cJSON_GetObjectItem(params, param_name);
}

static void wifi_schedule_restart(void)
{
    restart_pending = 1U;
    restart_deadline = tx_time_get() + wifi_ms_to_ticks(WIFI_RESTART_DELAY_MS);
}

static void wifi_handle_protocol_payload(cJSON *root)
{
    cJSON *data;
    cJSON *context;
    cJSON *item;
    const char *cmd;
    const char *req_id;
    unsigned int error_code = WIFI_ERROR_OK;
    const char *error_text = NULL;
    uint8_t target_switch;
    ULONG delay_s = 0U;
    int parsed_value;
    int publish_status = 0;
    int publish_telemetry = 0;
    int publish_config_event = 0;
    int restart_request_local = 0;

    data = cJSON_GetObjectItem(root, "data");
    req_id = wifi_get_req_id_text(root);
    if ((data != NULL) && cJSON_IsObject(data))
    {
        context = data;
    }
    else
    {
        context = root;
    }

    cmd = wifi_get_command_text(context);
    if (cmd == NULL)
    {
        wifi_publish_reply(req_id, "unknown", WIFI_ERROR_MISSING_FIELD, "missing cmd", 0U);
        return;
    }

    if (wifi_string_equals_any(cmd,
                               wifi_switch_cmd_aliases,
                               sizeof(wifi_switch_cmd_aliases) / sizeof(wifi_switch_cmd_aliases[0])) != 0U)
    {
        item = wifi_get_object_item_any(context,
                                        wifi_switch_keys,
                                        sizeof(wifi_switch_keys) / sizeof(wifi_switch_keys[0]));
        if (wifi_json_item_to_switch(item, &target_switch) == 0U)
        {
            error_code = WIFI_ERROR_INVALID_PARAM;
            error_text = "invalid switch";
        }
        else
        {
            timer_enable = 0U;
            auto_rule_active = 0U;
            wifi_load_set(target_switch);
            publish_status = 1;
            publish_telemetry = 1;
        }
    }
    else if (wifi_string_equals_any(cmd,
                                    wifi_switch_on_aliases,
                                    sizeof(wifi_switch_on_aliases) / sizeof(wifi_switch_on_aliases[0])) != 0U)
    {
        timer_enable = 0U;
        auto_rule_active = 0U;
        wifi_load_set(1U);
        publish_status = 1;
        publish_telemetry = 1;
    }
    else if (wifi_string_equals_any(cmd,
                                    wifi_switch_off_aliases,
                                    sizeof(wifi_switch_off_aliases) / sizeof(wifi_switch_off_aliases[0])) != 0U)
    {
        timer_enable = 0U;
        auto_rule_active = 0U;
        wifi_load_set(0U);
        publish_status = 1;
        publish_telemetry = 1;
    }
    else if (wifi_string_equals_any(cmd,
                                    wifi_timer_set_aliases,
                                    sizeof(wifi_timer_set_aliases) / sizeof(wifi_timer_set_aliases[0])) != 0U)
    {
        item = wifi_get_object_item_any(context,
                                        wifi_action_keys,
                                        sizeof(wifi_action_keys) / sizeof(wifi_action_keys[0]));
        if (wifi_json_item_to_action(item, &target_switch) == 0U)
        {
            if (strcmp(cmd, "delay_on") == 0)
            {
                target_switch = 1U;
            }
            else if (strcmp(cmd, "delay_off") == 0)
            {
                target_switch = 0U;
            }
            else
            {
                error_code = WIFI_ERROR_INVALID_PARAM;
                error_text = "invalid timer action";
            }
        }

        item = wifi_get_object_item_any(context,
                                        wifi_delay_keys,
                                        sizeof(wifi_delay_keys) / sizeof(wifi_delay_keys[0]));
        if ((error_code == WIFI_ERROR_OK) &&
            (wifi_json_item_to_int_range(item, 1, WIFI_TIMER_MAX_DELAY_S, &parsed_value) == 0U))
        {
            error_code = WIFI_ERROR_INVALID_PARAM;
            error_text = "delay_s out of range";
        }

        if (error_code == WIFI_ERROR_OK)
        {
            delay_s = (ULONG)parsed_value;
            timer_enable = 1U;
            timer_target_switch = target_switch;
            auto_rule_active = 0U;
            timer_deadline = tx_time_get() + (delay_s * TX_TIMER_TICKS_PER_SECOND);
            publish_status = 1;
            publish_telemetry = 1;
        }
    }
    else if (wifi_string_equals_any(cmd,
                                    wifi_timer_cancel_aliases,
                                    sizeof(wifi_timer_cancel_aliases) / sizeof(wifi_timer_cancel_aliases[0])) != 0U)
    {
        timer_enable = 0U;
        publish_status = 1;
        publish_telemetry = 1;
    }
    else if (wifi_string_equals_any(cmd,
                                    wifi_config_set_aliases,
                                    sizeof(wifi_config_set_aliases) / sizeof(wifi_config_set_aliases[0])) != 0U)
    {
        int config_changed = 0;

        item = wifi_get_object_item_any(context,
                                        wifi_report_period_keys,
                                        sizeof(wifi_report_period_keys) / sizeof(wifi_report_period_keys[0]));
        if (item != NULL)
        {
            if (wifi_json_item_to_int_range(item,
                                            (int)WIFI_REPORT_PERIOD_MIN_S,
                                            (int)WIFI_REPORT_PERIOD_MAX_S,
                                            &parsed_value) == 0U)
            {
                error_code = WIFI_ERROR_INVALID_PARAM;
                error_text = "report_period_s out of range";
            }
            else
            {
                report_period_seconds = (ULONG)parsed_value;
                config_changed = 1;
            }
        }

        if (error_code == WIFI_ERROR_OK)
        {
            item = wifi_get_object_item_any(context,
                                            wifi_temp_limit_keys,
                                            sizeof(wifi_temp_limit_keys) / sizeof(wifi_temp_limit_keys[0]));
            if (item != NULL)
            {
                if (wifi_json_item_to_int_range(item, -20, 80, &parsed_value) == 0U)
                {
                    error_code = WIFI_ERROR_INVALID_PARAM;
                    error_text = "temp_high_limit out of range";
                }
                else
                {
                    temp_high_limit = parsed_value;
                    config_changed = 1;
                }
            }
        }

        if (error_code == WIFI_ERROR_OK)
        {
            item = wifi_get_object_item_any(context,
                                            wifi_humidity_limit_keys,
                                            sizeof(wifi_humidity_limit_keys) / sizeof(wifi_humidity_limit_keys[0]));
            if (item != NULL)
            {
                if (wifi_json_item_to_int_range(item, 0, 100, &parsed_value) == 0U)
                {
                    error_code = WIFI_ERROR_INVALID_PARAM;
                    error_text = "humidity_high_limit out of range";
                }
                else
                {
                    humidity_high_limit = parsed_value;
                    config_changed = 1;
                }
            }
        }

        if (error_code == WIFI_ERROR_OK)
        {
            item = wifi_get_object_item_any(context,
                                            wifi_auto_rule_keys,
                                            sizeof(wifi_auto_rule_keys) / sizeof(wifi_auto_rule_keys[0]));
            if (item != NULL)
            {
                if (wifi_json_item_to_switch(item, &target_switch) == 0U)
                {
                    error_code = WIFI_ERROR_INVALID_PARAM;
                    error_text = "invalid auto_rule_enable";
                }
                else
                {
                    auto_rule_enable = target_switch;
                    if (auto_rule_enable == 0U)
                    {
                        auto_rule_active = 0U;
                    }
                    config_changed = 1;
                }
            }
        }

        if ((error_code == WIFI_ERROR_OK) && (config_changed == 0))
        {
            error_code = WIFI_ERROR_MISSING_FIELD;
            error_text = "missing config field";
        }

        if (error_code == WIFI_ERROR_OK)
        {
            publish_status = 1;
            publish_telemetry = 1;
            publish_config_event = 1;
        }
    }
    else if (wifi_string_equals_any(cmd,
                                    wifi_status_query_aliases,
                                    sizeof(wifi_status_query_aliases) / sizeof(wifi_status_query_aliases[0])) != 0U)
    {
        publish_status = 1;
        publish_telemetry = 1;
    }
    else if (wifi_string_equals_any(cmd,
                                    wifi_restart_aliases,
                                    sizeof(wifi_restart_aliases) / sizeof(wifi_restart_aliases[0])) != 0U)
    {
        publish_status = 1;
        restart_request_local = 1;
    }
    else
    {
        error_code = WIFI_ERROR_UNSUPPORTED_CMD;
        error_text = "unsupported cmd";
    }

    wifi_publish_reply(req_id, cmd, error_code, error_text, delay_s);

    if (error_code == WIFI_ERROR_OK)
    {
        if (publish_status != 0)
        {
            wifi_publish_status(1U, "connected");
        }
        if (publish_telemetry != 0)
        {
            wifi_publish_telemetry();
        }
        if (publish_config_event != 0)
        {
            wifi_publish_event("config_changed", "runtime configuration updated");
        }
        if (restart_request_local != 0)
        {
            wifi_publish_event("restart", "software reset requested");
            wifi_schedule_restart();
        }
    }
}

static void wifi_handle_legacy_payload(cJSON *root)
{
    cJSON *item;
    uint8_t target_switch;
    int changed = 0;

    item = wifi_get_json_item(root, "switch_set", "switch");
    if (wifi_json_item_to_switch(item, &target_switch) != 0U)
    {
        timer_enable = 0U;
        wifi_load_set(target_switch);
        changed = 1;
    }

    item = wifi_get_json_item(root, "timer_set", "timer_set");
    if ((item != NULL) && cJSON_IsNumber(item) && (item->valueint > 0))
    {
        timer_enable = 1U;
        timer_target_switch = 0U;
        timer_deadline = tx_time_get() + ((ULONG)item->valueint * TX_TIMER_TICKS_PER_SECOND);
        changed = 1;
    }

    item = wifi_get_json_item(root, "timer_cancel", "timer_cancel");
    if (item != NULL)
    {
        if ((cJSON_IsBool(item) && cJSON_IsTrue(item)) || (cJSON_IsNumber(item) && (item->valueint != 0)))
        {
            timer_enable = 0U;
            changed = 1;
        }
    }

    item = wifi_get_json_item(root, "config_set", "report_period_s");
    if ((item != NULL) && cJSON_IsNumber(item) &&
        (item->valueint >= (int)WIFI_REPORT_PERIOD_MIN_S) &&
        (item->valueint <= (int)WIFI_REPORT_PERIOD_MAX_S))
    {
        report_period_seconds = (ULONG)item->valueint;
        changed = 1;
    }

    if (changed != 0)
    {
        wifi_publish_status(1U, "connected");
        wifi_publish_telemetry();
    }
}

static void wifi_handle_payload(const char *payload)
{
    cJSON *root;
    const char *msg_type;
    cJSON *data;

    root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return;
    }

    msg_type = wifi_json_get_string(root, "msg_type");
    data = cJSON_GetObjectItem(root, "data");
    if (((msg_type != NULL) && (strcmp(msg_type, "cmd") == 0)) ||
        ((data != NULL) && cJSON_IsObject(data) && (wifi_get_command_text(data) != NULL)) ||
        (wifi_get_command_text(root) != NULL))
    {
        wifi_handle_protocol_payload(root);
    }
    else
    {
        wifi_handle_legacy_payload(root);
    }

    cJSON_Delete(root);
}

static void wifi_handle_subrecv(const char *line)
{
    const char *start;
    const char *end;
    char payload[WIFI_LINE_BUFFER_SIZE];
    uint16_t len;

    start = strchr(line, '{');
    end = strrchr(line, '}');
    if ((start == NULL) || (end == NULL) || (end < start))
    {
        return;
    }

    len = (uint16_t)(end - start + 1);
    if (len >= WIFI_LINE_BUFFER_SIZE)
    {
        len = WIFI_LINE_BUFFER_SIZE - 1U;
    }

    memcpy(payload, start, len);
    payload[len] = '\0';
    wifi_handle_payload(payload);
}

static void wifi_process_line(const char *line)
{
    if (strstr(line, "+MQTTSUBRECV:") != NULL)
    {
        wifi_handle_subrecv(line);
    }
    else if ((wifi_portal_active != 0U) && (strstr(line, "+IPD,") != NULL))
    {
        wifi_handle_http_request(line);
    }
    else if (strstr(line, "WIFI DISCONNECT") != NULL)
    {
        wifi_state = WIFI_STATE_FAIL;
        mqtt_state = WIFI_STATE_FAIL;
        last_rssi_update_tick = 0U;
        publish_enabled = 0U;
        if ((wifi_portal_active == 0U) && (wifi_sta_config_valid != 0U))
        {
            wifi_request = 1U;
            wifi_retry_deadline = 0U;
            mqtt_request = 0U;
        }
    }
    else if (strstr(line, "MQTTDISCONNECTED") != NULL)
    {
        mqtt_state = WIFI_STATE_FAIL;
        if ((wifi_portal_active == 0U) && (wifi_sta_config_valid != 0U) && (wifi_state == WIFI_STATE_OK))
        {
            mqtt_request = 1U;
        }
    }
}

static uint8_t wifi_read_line(char *line, uint16_t size)
{
    uint8_t data;
    static uint16_t len = 0U;

    while (wifi_rx_pop(&data) != 0U)
    {
        if (data == '\r')
        {
            continue;
        }

        if (data == '\n')
        {
            if (len == 0U)
            {
                continue;
            }
            line[len] = '\0';
            len = 0U;
            return 1U;
        }

        if (len < (size - 1U))
        {
            line[len++] = (char)data;
        }
        else
        {
            len = 0U;
        }
    }

    return 0U;
}

static void wifi_check_timer(void)
{
    if ((timer_enable != 0U) && ((LONG)(tx_time_get() - timer_deadline) >= 0))
    {
        timer_enable = 0U;
        auto_rule_active = 0U;
        wifi_load_set(timer_target_switch);
        wifi_publish_event("timer_expired", "scheduled action executed");
        wifi_publish_status(1U, "connected");
        wifi_publish_telemetry();
    }
}

static void wifi_check_auto_rule(void)
{
    uint8_t threshold_hit;

    if (auto_rule_enable == 0U)
    {
        auto_rule_active = 0U;
        return;
    }

    if (sensor_valid == 0U)
    {
        return;
    }

    threshold_hit = ((sensor_temperature >= temp_high_limit) ||
                     (sensor_humidity >= humidity_high_limit)) ? 1U : 0U;

    if ((threshold_hit != 0U) && (switch_state == 0U))
    {
        auto_rule_active = 1U;
        timer_enable = 0U;
        wifi_load_set(1U);
        wifi_publish_event("auto_rule_triggered", "threshold exceeded, output on");
        wifi_publish_status(1U, "connected");
        wifi_publish_telemetry();
    }
    else if ((threshold_hit == 0U) && (auto_rule_active != 0U))
    {
        auto_rule_active = 0U;
        timer_enable = 0U;
        wifi_load_set(0U);
        wifi_publish_event("auto_rule_cleared", "threshold recovered, output off");
        wifi_publish_status(1U, "connected");
        wifi_publish_telemetry();
    }
}

static void wifi_check_restart(void)
{
    if ((restart_pending != 0U) && ((LONG)(tx_time_get() - restart_deadline) >= 0))
    {
        restart_pending = 0U;
        NVIC_SystemReset();
    }
}

void Wifi_Init(void)
{
    wifi_gpio_init();
    wifi_usart_init();
    wifi_rx_clear();
    wifi_load_sta_config();
    wifi_state = WIFI_STATE_IDLE;
    mqtt_state = WIFI_STATE_IDLE;
    wifi_step = WIFI_WIFI_STEP_NONE;
    mqtt_step = WIFI_MQTT_STEP_NONE;
    wifi_request = 1U;
    mqtt_request = wifi_sta_config_valid;
    clear_config_request = 0U;
    publish_request = 0U;
    publish_enabled = 0U;
    timer_enable = 0U;
    timer_target_switch = 0U;
    auto_rule_enable = 0U;
    auto_rule_active = 0U;
    restart_pending = 0U;
    wifi_portal_active = 0U;
    wifi_reset_retry_state();
    wifi_set_default_ap_ssid();
    report_period_seconds = WIFI_DEFAULT_REPORT_PERIOD_S;
    last_rssi_update_tick = 0U;
    wifi_rssi = WIFI_DEFAULT_RSSI;
    temp_high_limit = WIFI_DEFAULT_TEMP_HIGH_LIMIT;
    humidity_high_limit = WIFI_DEFAULT_HUMIDITY_HIGH_LIMIT;
}

void Wifi_ThreadEntry(ULONG thread_input)
{
    char line[WIFI_LINE_BUFFER_SIZE];
    ULONG last_publish_tick = 0U;
    ULONG last_heartbeat_tick = 0U;

    (void)thread_input;

    Wifi_Init();

    while (1)
    {
        while (wifi_read_line(line, sizeof(line)) != 0U)
        {
            wifi_process_line(line);
        }

        if (clear_config_request != 0U)
        {
            clear_config_request = 0U;
            wifi_request = 0U;
            mqtt_request = 0U;
            publish_request = 0U;
            publish_enabled = 0U;
            if (wifi_clear_sta_config() != 0U)
            {
                wifi_schedule_restart();
            }
        }

        if (wifi_retry_ready() != 0U)
        {
            wifi_request = 0U;
            wifi_retry_deadline = 0U;
            if (wifi_connect_wifi() == 0U)
            {
                publish_enabled = 0U;
                if ((wifi_sta_config_valid != 0U) && (wifi_portal_active == 0U))
                {
                    wifi_schedule_retry_after_failure();
                }
            }
            else
            {
                mqtt_request = 1U;
            }
        }

        if (mqtt_request != 0U)
        {
            mqtt_request = 0U;
            wifi_connect_broker();
        }

        if (publish_request != 0U)
        {
            publish_request = 0U;
            publish_enabled = 1U;
            last_publish_tick = 0U;
        }

        wifi_check_timer();
        wifi_check_auto_rule();
        wifi_check_restart();

        if ((publish_enabled != 0U) && (mqtt_state == WIFI_STATE_OK))
        {
            if ((last_publish_tick == 0U) ||
                ((ULONG)(tx_time_get() - last_publish_tick) >= wifi_ms_to_ticks(report_period_seconds * 1000U)))
            {
                last_publish_tick = tx_time_get();
                wifi_publish_telemetry();
            }
        }

        if (mqtt_state == WIFI_STATE_OK)
        {
            if ((last_heartbeat_tick == 0U) ||
                ((ULONG)(tx_time_get() - last_heartbeat_tick) >= wifi_ms_to_ticks(WIFI_HEARTBEAT_PERIOD_S * 1000U)))
            {
                last_heartbeat_tick = tx_time_get();
                wifi_publish_heartbeat();
            }
        }

        tx_thread_sleep(1U);
    }
}

void Wifi_USART3_IRQHandler(void)
{
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        wifi_rx_push((uint8_t)USART_ReceiveData(USART3));
    }

    if (USART_GetFlagStatus(USART3, USART_FLAG_ORE) != RESET)
    {
        (void)USART_ReceiveData(USART3);
    }
}

void Wifi_GetStatus(WIFI_STATUS *status)
{
    if (status == NULL)
    {
        return;
    }

    status->wifi = wifi_state;
    status->mqtt = mqtt_state;
    status->wifi_step = wifi_step;
    status->mqtt_step = mqtt_step;
    status->publish_enabled = publish_enabled;
}

const char *Wifi_GetApSsid(void)
{
    return wifi_ap_ssid;
}

void Wifi_UpdateSensor(uint8_t temperature, uint8_t humidity, uint8_t valid)
{
    sensor_temperature = temperature;
    sensor_humidity = humidity;
    sensor_valid = valid;
}

uint8_t Wifi_IsSwitchOn(void)
{
    return switch_state;
}

void Wifi_RequestWifiConnect(void)
{
    wifi_request = 1U;
    wifi_retry_deadline = 0U;
}

void Wifi_RequestMqttConnect(void)
{
    mqtt_request = 1U;
}

void Wifi_RequestPublishStart(void)
{
    publish_request = 1U;
}

void Wifi_RequestClearConfig(void)
{
    clear_config_request = 1U;
}
