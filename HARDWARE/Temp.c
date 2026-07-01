#include "Temp.h"

#define DHT11_PORT        GPIOA
#define DHT11_PIN         GPIO_Pin_7
#define DHT11_RCC         RCC_AHB1Periph_GPIOA
#define DHT11_READ()      GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN)
#define DHT11_HIGH()      GPIO_SetBits(DHT11_PORT, DHT11_PIN)
#define DHT11_LOW()       GPIO_ResetBits(DHT11_PORT, DHT11_PIN)

static void Temp_DelayUs(uint32_t us)
{
	uint32_t start;
	uint32_t ticks;

	if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
	{
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
		DWT->CYCCNT = 0U;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	}

	ticks = (SystemCoreClock / 1000000U) * us;
	start = DWT->CYCCNT;
	while ((DWT->CYCCNT - start) < ticks)
	{
	}
}

static void Temp_SetOutput(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	GPIO_InitStructure.GPIO_Pin = DHT11_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DHT11_PORT, &GPIO_InitStructure);
}

static void Temp_SetInput(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	GPIO_InitStructure.GPIO_Pin = DHT11_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DHT11_PORT, &GPIO_InitStructure);
}

static uint8_t Temp_WaitLevel(BitAction level, uint32_t timeout_us)
{
	while (DHT11_READ() != (uint8_t)level)
	{
		if (timeout_us == 0U)
		{
			return TEMP_ERROR;
		}
		timeout_us--;
		Temp_DelayUs(1U);
	}

	return TEMP_OK;
}

static uint8_t Temp_ReadByte(uint8_t *byte)
{
	uint8_t i;
	uint8_t data = 0U;

	if (byte == 0)
	{
		return TEMP_ERROR;
	}

	for (i = 0U; i < 8U; i++)
	{
		if (Temp_WaitLevel(Bit_RESET, 100U) != TEMP_OK)
		{
			return TEMP_ERROR;
		}
		if (Temp_WaitLevel(Bit_SET, 100U) != TEMP_OK)
		{
			return TEMP_ERROR;
		}

		Temp_DelayUs(40U);
		data <<= 1U;
		if (DHT11_READ() == Bit_SET)
		{
			data |= 1U;
		}

		if (Temp_WaitLevel(Bit_RESET, 100U) != TEMP_OK)
		{
			return TEMP_ERROR;
		}
	}

	*byte = data;

	return TEMP_OK;
}

void Temp_Init(void)
{
	RCC_AHB1PeriphClockCmd(DHT11_RCC, ENABLE);
	Temp_SetOutput();
	DHT11_HIGH();
}

uint8_t Temp_Read(uint8_t *temperature, uint8_t *humidity)
{
	uint8_t data[5];
	uint8_t i;
	uint8_t status = TEMP_OK;
	uint32_t primask;

	if ((temperature == 0) || (humidity == 0))
	{
		return TEMP_ERROR;
	}

	Temp_SetOutput();
	DHT11_LOW();
	Temp_DelayUs(20000U);

	primask = __get_PRIMASK();
	__disable_irq();

	DHT11_HIGH();
	Temp_DelayUs(30U);
	Temp_SetInput();

	if (Temp_WaitLevel(Bit_RESET, 100U) != TEMP_OK)
	{
		status = TEMP_ERROR;
		goto read_done;
	}
	if (Temp_WaitLevel(Bit_SET, 100U) != TEMP_OK)
	{
		status = TEMP_ERROR;
		goto read_done;
	}
	if (Temp_WaitLevel(Bit_RESET, 100U) != TEMP_OK)
	{
		status = TEMP_ERROR;
		goto read_done;
	}

	for (i = 0U; i < 5U; i++)
	{
		if (Temp_ReadByte(&data[i]) != TEMP_OK)
		{
			status = TEMP_ERROR;
			goto read_done;
		}
	}

	if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
	{
		status = TEMP_ERROR;
		goto read_done;
	}

	*humidity = data[0];
	*temperature = data[2];

read_done:
	if (primask == 0U)
	{
		__enable_irq();
	}
	Temp_SetOutput();
	DHT11_HIGH();

	return status;
}
