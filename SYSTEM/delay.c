#include "stm32f4xx.h"
#include "delay.h"

//微秒级延时函数
void Delay_us(uint32_t nus)
{
	//用于记录重装载寄存器的值
	uint32_t temp;
	
	SysTick->LOAD = 21*nus;                  //重装载寄存器的值
	SysTick->VAL = 0x00;                     //清空计数器
	SysTick->CTRL = 0x01;                    //开启计数器
	
	do
	{
		temp = SysTick->CTRL;
		}while((temp&0x01)&&!(temp&(1<<16))); //等待计数到0（控制与状态寄存器的第0位为0且第16位为1，则计数到0）
	
	SysTick->CTRL = 0x00;                    //关闭计数器
	SysTick->VAL = 0x00;                     //清空计数器
}

//毫秒级延时函数过渡函数
void Delay_xms(uint32_t nxms)
{
	//用于记录重装载寄存器的值
	uint32_t temp;
	
	SysTick->LOAD = 21000*nxms;              //重装载寄存器的值
	SysTick->VAL = 0x00;                     //清空计数器
	SysTick->CTRL = 0x01;                    //开启计数器
	
	do
	{
		temp = SysTick->CTRL;
		}while((temp&0x01)&&!(temp&(1<<16))); //等待计数到0（控制与状态寄存器的第0位为0且第16位为1，则计数到0）
	
	SysTick->CTRL = 0x00;                    //关闭计数器
	SysTick->VAL = 0x00;                     //清空计数器
}

//毫秒级延时函数：当延时时间大于798ms时，需要多次调用过渡函数Delay_xms，小于798ms时本函数也适用
void Delay_ms(uint32_t nms)
{
	//设置商和余数
	uint32_t quotient_value = nms/798;
	uint32_t remainder_value = nms%798;
	
	while(quotient_value--)
	{
		Delay_xms(798);
	}
	if(remainder_value)
	{
		Delay_xms(remainder_value);
	}
}

