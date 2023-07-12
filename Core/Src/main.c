#include "main.h"
#include "cmsis_os.h"
UART_HandleTypeDef huart1;
osThreadId_t defaultTaskHandle;
osThreadId_t defaultTaskHandle1;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
const osThreadAttr_t defaultTask_attributes1 = {
  .name = "defaultTask1",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
enum eWTT_State {
		WTT_POWER_DOWN,
		WTT_GET_PARAM,
		WTT_POWER_WAKEUP,
		WTT_START_HEATER,
		WTT_SUPPORT_HEATER,
		WTT_STOPHEATER
		};

const uint32_t WBUS_TX_DELAY = 100; // 100ms delay for next sending

struct str_WEBASTO_WORK_PARAM {
		uint8_t  TotalTimeWork; // 0...30min
		uint16_t CurrTimeWork;		// 0...1800 sec
		uint8_t  WorkStatus; 	// 1-work, 0-not work
		uint16_t  CurrUakb;		// current AKB voltage *1000
		signed char CurrT;				// current Temperature
		uint8_t  StartCMD;		// 1-start heater command
		uint8_t  StopCMD;			// 1-alarm stop heater command
		uint16_t MaxWorkTime_sec;
		} WEBASTO_WORK_PARAM = {0,0,0,0,0,0,0,900};

uint8_t WBUS_TX(char* wbus_tx_cmd,uint8_t tx_len,char* ack, uint8_t ack_len, uint8_t rx_try_cnt, uint16_t tx_try_delay);
uint8_t WBUS_TX_EXT(char* wbus_tx_cmd,uint8_t tx_len,char* ack, uint8_t ack_len, uint8_t rx_try_cnt, uint16_t tx_try_delay);
enum eWTT_State WTT_State =  WTT_POWER_DOWN;
void WTT_Change_state(enum eWTT_State newstate);
char 	 WBUS_ACK_EXT[200];
void USART1_UART_Init_8E1(uint16_t speed);
void SystemClock_Config(void);
void webasto_func(void *argument);

uint8_t button_press()
{
	uint8_t button_count = 0;
	//if(HAL_GPIO_ReadPin(GPIOA, button_on_off_Pin))
	if(HAL_GPIO_ReadPin(button_on_off_GPIO_Port, button_on_off_Pin))
	{
		while(HAL_GPIO_ReadPin(button_on_off_GPIO_Port, button_on_off_Pin) /*== GPIO_PIN_SET*/)
		{
			button_count = button_count + 1;
			osDelay(100);
		}
	}
	else{button_count = 0; return 0;}
	if(button_count <= 20 && button_count >= 10)
		return 1; //shot press button
	else if(button_count > 20 && button_count < 800)
