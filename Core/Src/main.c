/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_alarm.h"
#include "app_cmd.h"
#include "app_dbg.h"
#include "app_dust.h"
#include "app_cfg.h"
#include "app_lcd.h"
#include "app_net.h"
#include "app_relay.h"
#include "app_touch.h"
#include "app_ui.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void on_touch_click(uint16_t x, uint16_t y);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  /* 粉尘链路：先校准 ADC，再让 TIM3 产生采样窗口与触发，最后等 TIM3_TRGO */
  app_dust_calibrate();
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);   /* PA6：传感器 LED，10 ms / 320 us */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);   /* PA7：280 us 处 → TRGO 触发 ADC */
  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }


  app_relay_init();                           /* 6 路继电器先全部断开 */
  app_dust_init();
  app_alarm_init();                           /* 蜂鸣器/状态灯静音 */
  app_cmd_init();                             /* USART1 命令接收就绪 */
  app_net_init();                             /* USART3 + ESP8266 */
  app_ui_init();
  app_lcd_init();                             /* 3.5 寸 TFTLCD（无屏时会自己报错跳过） */
  app_touch_init();                           /* XPT2046 电阻触摸（已存校准就直接用） */

  dbg_write("\r\nF407_esp8266 ready. type 'help' for commands.\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t tick_10ms = HAL_GetTick();
  uint32_t tick_200ms = tick_10ms;
  uint32_t tick_1s = tick_10ms;

  while (1)
  {
    uint32_t now = HAL_GetTick();

    if ((now - tick_10ms) >= 10u)             /* 10 ms 时基：命令、采样、报警、联网 */
    {
      tick_10ms = now;
      app_cmd_task();
      app_dust_task();
      app_alarm_task(app_dust_level());
      app_net_task();
      app_touch_task(on_touch_click);
    }

    if ((now - tick_200ms) >= 200u)           /* 200 ms 时基：屏幕局部刷新 */
    {
      tick_200ms = now;
      app_lcd_update();
    }

    if ((now - tick_1s) >= 1000u)             /* 1 s 时基：状态输出 */
    {
      tick_1s = now;
      app_ui_task();
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
  * @brief  触摸回调：点继电器方块 → 翻转该路；点 ALL OFF → 全断
  * @note   屏幕布局坐标由 app_lcd_hit_test() 判定，本函数只管动作
  */
static void on_touch_click(uint16_t x, uint16_t y)
{
  uint8_t hit = app_lcd_hit_test(x, y);

  if (hit >= 1u && hit <= APP_RELAY_COUNT)
  {
    app_relay_toggle(hit);
    app_lcd_update();
  }
  else if (hit == APP_LCD_BTN_ALL_OFF)
  {
    app_relay_set_mode(0u);
    app_lcd_update();
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
