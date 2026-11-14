#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#define APP_TAG "rtos_safe"

/*
 * 0: volatile state only; attempts, alarm and changed PIN are lost on reboot.
 * 1: persist the security state and PIN in the external FRAM.
 */
#define APP_PERSISTENT_STORAGE_ENABLED 0

/* Shared I2C bus: SSD1306 OLED, DS1307 RTC and PN532 NFC reader. */
#define APP_I2C_PORT            I2C_NUM_0
#define APP_I2C_SDA_GPIO        GPIO_NUM_40
#define APP_I2C_SCL_GPIO        GPIO_NUM_2
#define APP_I2C_INTERNAL_PULLUP 1

/* MB85RC256V non-volatile FRAM on the shared I2C bus. */
#define APP_FRAM_I2C_ADDRESS  0x50U
#define APP_FRAM_I2C_CLOCK_HZ 100000U

/* Safe-state indication LEDs. */
#define APP_LED_RED_GPIO     GPIO_NUM_8
#define APP_LED_YELLOW_GPIO  GPIO_NUM_11
#define APP_LED_GREEN_GPIO   GPIO_NUM_41
#define APP_LED_ACTIVE_HIGH  1

/* Audio output. */
#define APP_AUDIO_GPIO              GPIO_NUM_9
#define APP_ALARM_SOUND_DURATION_MS 10000U

/* Rotary encoder and its push button. */
#define APP_ENCODER_A_GPIO           GPIO_NUM_18
#define APP_ENCODER_B_GPIO           GPIO_NUM_21
#define APP_ENCODER_SW_GPIO          GPIO_NUM_10
#define APP_ENCODER_COUNTS_PER_STEP  4
#define APP_ENCODER_GLITCH_FILTER_NS 1000U
#define APP_BUTTON_ACTIVE_HIGH       0
#define APP_BUTTON_DEBOUNCE_MS       50U
#define APP_BUTTON_LONG_PRESS_MS     4000U

/* Lock actuator: GPIO7 drives a low-trigger relay through an NPN stage. */
#define APP_RELAY_GPIO               GPIO_NUM_7
#define APP_RELAY_CONTROL_ACTIVE_LOW 0
#define APP_SERVO_PWM_GPIO           GPIO_NUM_4
#define APP_SERVO_LOCKED_PULSE_US    700U
#define APP_SERVO_OPEN_PULSE_US      2300U
#define APP_SERVO_POWER_STABILIZE_MS 100U
#define APP_SERVO_MOVEMENT_TIME_MS   1000U

/* Safe state machine. */
#define APP_SAFE_CODE_LENGTH        4U
#define APP_SAFE_MAX_ATTEMPTS       3U
#define APP_SAFE_ENTRY_TIMEOUT_MS   30000U
#define APP_SAFE_OPEN_TIMEOUT_MS    10000U
#define APP_SAFE_ALARM_TIMEOUT_MS   (3U * 60U * 60U * 1000U)
#define APP_SAFE_EVENT_QUEUE_LENGTH 16U
#define APP_SAFE_TASK_STACK_SIZE    4096U
#define APP_SAFE_TASK_PRIORITY      5U

/* Display task. */
#define APP_DISPLAY_QUEUE_LENGTH 1U
#define APP_DISPLAY_STACK_SIZE   4096U
#define APP_DISPLAY_PRIORITY     4U

/* Access audit task and flash partition. */
#define APP_AUDIT_PARTITION_LABEL   "audit"
#define APP_AUDIT_PARTITION_SUBTYPE 0x40
#define APP_AUDIT_QUEUE_LENGTH      8U
#define APP_AUDIT_STACK_SIZE        4096U
#define APP_AUDIT_TASK_PRIORITY     3U

/* PN532 NFC reader and administrator card. */
#define APP_NFC_I2C_ADDRESS              0x24U
#define APP_NFC_I2C_CLOCK_HZ             100000U
#define APP_NFC_ADMIN_UID_LENGTH         4U
#define APP_NFC_ADMIN_UID                {0xAAU, 0xEDU, 0xE2U, 0x8BU}
