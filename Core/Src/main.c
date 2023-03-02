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
		return 2; //long press button
	else
		return 0;
}
void info_led_light(uint32_t sec)
{
	//HAL_GPIO_WritePin(led_info_GPIO_Port, led_info_Pin, GPIO_PIN_RESET);
	//osDelay(sec);
	HAL_GPIO_WritePin(led_info_GPIO_Port, led_info_Pin, GPIO_PIN_SET);
	//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET);
	osDelay(sec);
	//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(led_info_GPIO_Port, led_info_Pin, GPIO_PIN_RESET);
	osDelay(sec);
	//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(led_info_GPIO_Port, led_info_Pin, GPIO_PIN_SET);
}
void rele_func()
{
	for(;;)
	  {
		if(WEBASTO_WORK_PARAM.WorkStatus == 1)
		{
			HAL_GPIO_WritePin(rele_pomp_GPIO_Port, rele_pomp_Pin, GPIO_PIN_SET); //rele on
		}
		else if (WTT_State == WTT_STOPHEATER)
		{

			osDelay(500);
			HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET); //led on
			osDelay(500);
			HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_RESET); //led on
			osDelay(500);
			HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET); //led on
			osDelay(500);
			HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_RESET); //led on
			osDelay(500);
			for (int i = 0; i < 60; ++i) {
				osDelay(1000);
			}
			HAL_GPIO_WritePin(rele_pomp_GPIO_Port, rele_pomp_Pin, GPIO_PIN_RESET); //rele on
		}
		osDelay(100);
	  }
}
void WEBASTO_Proc() {

		const char WB_GET_PARAM[] =  {0x24,0x05,0x50,0x30,0x0c,0x0e,0x43}; // ack 42 08 d0 30 0c 2e 0e 30 52 e4
		const char WB_Tcode_SHIFT = 4;
		const char WB_Tvalue_SHIFT = 5;
		const char WB_Ucode_SHIFT = 6;
		const char WB_Uvalue_SHIFT = 7;
		const char WB_GET_PARAM_PACKSIZE = 10;
		const char WB_START_CMD[] = {0x24,0x03,0x21,0x0F,0x09};  // 24=from Telestart to Heater,  0F=15 min work
		const char WB_START_CMD_ACK[] = {0x42,0x03,0xA1,0x0F,0xEF};
		const char WB_STARTSUPP_CMD[] = {0x24,0x04,0x44,0x21,0x00,0x45};
		const char WB_STARTSUPP_CMD_ACK[] = {0x42,0x03,0xA1,0x0F,0xEF};
		const char WB_STOP_CMD[] = {0x24,0x02,0x10,0x36}; //
		const char WB_STOP_CMD_ACK[] = {0x42,0x02,0x90,0xD0};
		const char null = 0;
		uint8_t ret_code=0;
		static char tempstr[100]={0};

		for(int i=0;i<sizeof(tempstr);i++) tempstr[i]=0; // clear

		switch (WTT_State) {
				case (WTT_POWER_DOWN): {
						osDelay(10);
						WEBASTO_WORK_PARAM.CurrTimeWork=WEBASTO_WORK_PARAM.TotalTimeWork*60;
						if((button_press() == 1 || button_press() == 2) && WEBASTO_WORK_PARAM.WorkStatus==0)
						{
							WTT_Change_state(WTT_POWER_WAKEUP);
							info_led_light(300);
							//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_RESET);
							//osDelay(300);
							//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET);
							//HAL_GPIO_WritePin(GPIOA, led_on_off_Pin, GPIO_PIN_SET); //led on
						} break;
				}
				case (WTT_POWER_WAKEUP): {
						// Wake-Up Heater
						USART1_UART_Init_8E1(360); // 25ms K-line on down
						ret_code = WBUS_TX((char*)&null,1,NULL,1,1,500);
						osDelay(25);							// 25ms  K-line on up
						WTT_Change_state(WTT_GET_PARAM);
				} /*break*/;
				case (WTT_GET_PARAM): {
						// Get param T U
						USART1_UART_Init_8E1(2400);
						ret_code = WBUS_TX_EXT((char*)WB_GET_PARAM,sizeof(WB_GET_PARAM),(char*)WBUS_ACK_EXT,WB_GET_PARAM_PACKSIZE,4,500);
						if(ret_code==0) { // WTT not respond
								WTT_Change_state(WTT_POWER_DOWN);
								info_led_light(100);
								info_led_light(500);
								//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET);
								//osDelay(1000);
								//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET); //led off
								}
						else {
								if(WBUS_ACK_EXT[WB_Tcode_SHIFT]==0x0c) WEBASTO_WORK_PARAM.CurrT = WBUS_ACK_EXT[WB_Tvalue_SHIFT]-50;
								if(WBUS_ACK_EXT[WB_Ucode_SHIFT]==0x0e) WEBASTO_WORK_PARAM.CurrUakb = (uint16_t) ((WBUS_ACK_EXT[WB_Uvalue_SHIFT]<<8) | WBUS_ACK_EXT[WB_Uvalue_SHIFT+1]);
								if(WEBASTO_WORK_PARAM.WorkStatus==0) {
										WTT_Change_state(WTT_START_HEATER);
										HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET); //led on
										//HAL_GPIO_WritePin(rele_pomp_GPIO_Port, rele_pomp_Pin, GPIO_PIN_SET); //rele on
										}
								else if(WEBASTO_WORK_PARAM.WorkStatus==1) {
										WTT_Change_state(WTT_SUPPORT_HEATER);
										}
								}
						osDelay(50);
						} break;
				case (WTT_START_HEATER): {
						// Start Heater
						USART1_UART_Init_8E1(2400);
						ret_code = WBUS_TX((char*)WB_START_CMD,sizeof(WB_START_CMD),(char*)WB_START_CMD_ACK,sizeof(WB_START_CMD_ACK),4,500);
						if(ret_code==0) { // WTT not respond
								info_led_light(500);
								info_led_light(100);
								WTT_Change_state(WTT_STOPHEATER);
								}
						else {
								WTT_Change_state(WTT_SUPPORT_HEATER);
								WEBASTO_WORK_PARAM.WorkStatus=1;
								}
						} break;
				case (WTT_SUPPORT_HEATER): {
						osDelay(10000);
						ret_code = WBUS_TX((char*)WB_STARTSUPP_CMD,sizeof(WB_STARTSUPP_CMD),(char*)WB_STARTSUPP_CMD_ACK,sizeof(WB_STARTSUPP_CMD_ACK),3,500);
						if(ret_code==0) { // WTT not respond
								//info_led_light(500);
								//info_led_light(500);
								//info_led_light(500);
								WTT_Change_state(WTT_STOPHEATER);
						}
						if((button_press() == 1 || button_press() == 2) && WEBASTO_WORK_PARAM.WorkStatus==1)
					    {
							WTT_Change_state(WTT_STOPHEATER);
							//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET); //led on
							//osDelay(1000);
							//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_RESET); //led on
							//osDelay(1000);
							info_led_light(500);
							info_led_light(500);
							info_led_light(500);
							//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET); //led on

						}
						if(WEBASTO_WORK_PARAM.CurrT > 50)
						{
							WTT_Change_state(WTT_STOPHEATER);
							info_led_light(500);
							info_led_light(1000);
							//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET); //led off
						}
						if((double)WEBASTO_WORK_PARAM.CurrUakb/1000 < 12.3)
						{
							WTT_Change_state(WTT_STOPHEATER);
							info_led_light(500);
							info_led_light(1000);
							info_led_light(2000);
							//HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_SET); //led off
						}

						WEBASTO_WORK_PARAM.CurrTimeWork = WEBASTO_WORK_PARAM.CurrTimeWork-10;
						if(WEBASTO_WORK_PARAM.CurrTimeWork<=20)
								WTT_Change_state(WTT_STOPHEATER);
						else {
								WTT_Change_state(WTT_GET_PARAM); //request last param's V T
								osDelay(200);
								}

						}	break;

				case (WTT_STOPHEATER): {
					//TODO помпу надо дальше крутить!!!!!
						//HAL_GPIO_WritePin(rele_pomp_GPIO_Port, rele_pomp_Pin, GPIO_PIN_RESET); //rele off
						osDelay(1000);
						// Stop heater
						ret_code = WBUS_TX((char*)WB_STOP_CMD,sizeof(WB_STOP_CMD),(char*)WB_STOP_CMD_ACK,sizeof(WB_STOP_CMD_ACK),3,500);
						//if(WEBASTO_WORK_PARAM.StopCMD==1) WEBASTO_WORK_PARAM.StopCMD=0;
						WEBASTO_WORK_PARAM.WorkStatus=0;

						HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_RESET); //led off

						WTT_Change_state(WTT_POWER_DOWN);
						}	break;
				}
}
uint8_t WBUS_TX(char* wbus_tx_cmd,uint8_t tx_len,char* ack, uint8_t ack_len, uint8_t rx_try_cnt, uint16_t tx_try_delay) {
		char WB_ACK[100]={0,0,0,0,0,0,0,0,0,0};
		char parazit_byte;
		uint16_t i;
		uint8_t mismatch=0;
		uint8_t wbus_rx_done=0;
		uint8_t result=0;

		static uint32_t tx_delay_tmp;

		while(tx_delay_tmp>HAL_GetTick()) {
				//taskYIELD();
				osDelay(10);
				}

		do {
				for(int i=0;i<sizeof(WB_ACK);i++) WB_ACK[i]=0; // clear buf

				HAL_UART_Transmit(&huart1,(uint8_t*)wbus_tx_cmd,tx_len,100);
				if(ack_len==0) {result=1; break;} // out of cycle
				if(--rx_try_cnt==0) {result=0; break;} // out of cycle

				HAL_UART_Receive(&huart1,(uint8_t*)&parazit_byte,1,1);
				HAL_UART_Receive(&huart1,(uint8_t*)WB_ACK,ack_len,200);
				for(i=0;i<ack_len;i++) {
						if(WB_ACK[i] != ack[i]) mismatch++;
						}
				if (mismatch>0) {
						wbus_rx_done=0;
						osDelay(tx_try_delay);
						}
				else	{
						wbus_rx_done=1;
						result=1;
						}
				mismatch=0;

				} while (wbus_rx_done==0);

		tx_delay_tmp = (HAL_GetTick()<0xFFFFFFFF-WBUS_TX_DELAY) ? (HAL_GetTick() + WBUS_TX_DELAY) : (0); // set pause between w-bus sending
		if(result==1) return 1;
		else return 0; //
		}

uint8_t WBUS_TX_EXT(char* wbus_tx_cmd,uint8_t tx_len,char* ack, uint8_t ack_len, uint8_t rx_try_cnt, uint16_t tx_try_delay)
{
		char parazit_byte;
		//uint16_t i;
//		uint8_t mismatch=0;
		uint8_t wbus_rx_done=0;
		static uint32_t tx_delay_tmp;
		uint8_t result=0;

		while(tx_delay_tmp>HAL_GetTick()) {
				osDelay(10);
				}

		do {
				for(int i=0;i<sizeof(WBUS_ACK_EXT);i++) WBUS_ACK_EXT[i]=0; // clear buf
				HAL_UART_Transmit(&huart1,(uint8_t*)wbus_tx_cmd,tx_len,100);

				if(ack_len==0) {result=1; break;} // out of cycle
				if(--rx_try_cnt==0) {result=0; break;} // out of cycle



				HAL_UART_Receive(&huart1,(uint8_t*)&parazit_byte,1,1); // rx parasite byte
				if(HAL_UART_Receive(&huart1,(uint8_t*)WBUS_ACK_EXT,ack_len,200)==HAL_OK) {
				//if(strlen(WBUS_ACK_EXT) == ack_len) {
						wbus_rx_done=1;
						result=1;
						}
				else osDelay(tx_try_delay);

		} while (wbus_rx_done==0);

		tx_delay_tmp = (HAL_GetTick()<0xFFFFFFFF-WBUS_TX_DELAY) ? (HAL_GetTick() + WBUS_TX_DELAY) : (0); // set pause between w-bus sending
		if(result==1) return 1;
		else return 0; //
}

void USART1_UART_Init_8E1(uint16_t speed)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = speed;
  huart1.Init.WordLength = UART_WORDLENGTH_9B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_EVEN;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
}

void WTT_Change_state(enum eWTT_State newstate)
{
		WTT_State = newstate;
}


int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  osKernelInitialize();
  defaultTaskHandle = osThreadNew(webasto_func, NULL, &defaultTask_attributes);
  defaultTaskHandle1 = osThreadNew(rele_func, NULL, &defaultTask_attributes1);
  osKernelStart();

  while (1)
  {
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 11500;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}
void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(led_info_GPIO_Port, led_info_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, led_on_off_Pin|rele_pomp_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : led_info_Pin */
  GPIO_InitStruct.Pin = led_info_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(led_info_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : button_on_off_Pin */
  GPIO_InitStruct.Pin = button_on_off_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(button_on_off_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : led_on_off_Pin rele_pomp_Pin */
  GPIO_InitStruct.Pin = led_on_off_Pin|rele_pomp_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}

void webasto_func(void *argument)
{
  for(;;)
  {
	  HAL_GPIO_WritePin(led_info_GPIO_Port, led_info_Pin, GPIO_PIN_SET);
	  HAL_GPIO_WritePin(led_on_off_GPIO_Port, led_on_off_Pin, GPIO_PIN_RESET);
	  HAL_GPIO_WritePin(rele_pomp_GPIO_Port, rele_pomp_Pin, GPIO_PIN_RESET); //rele off
	WEBASTO_Proc();
    osDelay(1);
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
