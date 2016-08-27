#ifndef MAIN_H__
#define MAIN_H__

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "bsp.h"
#include "bsp_btn.h"
#include "pstorage.h"
#include "nordic_common.h"
#include "nrf_gpio.h"
#include "nrf_error.h"
#include "nrf_drv_clock.h"
#include "sdk_errors.h"
#include "app_error.h"
#include "app_timer.h"

#define APP_TASK_BLE_STACK_SIZE            256     /**< Size of the BLE task stack. */
#define APP_TASK_BLE_PRIORITY                1     /**< Priority of the BLE task. */

#endif
