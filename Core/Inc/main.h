/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define OLED_DC_Pin GPIO_PIN_9
#define OLED_DC_GPIO_Port GPIOB
#define OLED_CS_Pin GPIO_PIN_9
#define OLED_CS_GPIO_Port GPIOI
#define LED_8_Pin GPIO_PIN_8
#define LED_8_GPIO_Port GPIOG
#define LED_7_Pin GPIO_PIN_7
#define LED_7_GPIO_Port GPIOG
#define LED_6_Pin GPIO_PIN_6
#define LED_6_GPIO_Port GPIOG
#define LED_5_Pin GPIO_PIN_5
#define LED_5_GPIO_Port GPIOG
#define LED_4_Pin GPIO_PIN_4
#define LED_4_GPIO_Port GPIOG
#define LED_3_Pin GPIO_PIN_3
#define LED_3_GPIO_Port GPIOG
#define LED_2_Pin GPIO_PIN_2
#define LED_2_GPIO_Port GPIOG
#define KEY_Pin GPIO_PIN_2
#define KEY_GPIO_Port GPIOB
#define LED_1_Pin GPIO_PIN_1
#define LED_1_GPIO_Port GPIOG
#define TFT_CS_Pin GPIO_PIN_6
#define TFT_CS_GPIO_Port GPIOA
#define SD_DETECT_Pin GPIO_PIN_15
#define SD_DETECT_GPIO_Port GPIOE
#define OLED_RST_Pin GPIO_PIN_10
#define OLED_RST_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
