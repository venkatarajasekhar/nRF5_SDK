/* Copyright (c) 2015 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/** @file
 * @defgroup blinky_example_main main.c
 * @{
 * @ingroup blinky_example_freertos
 *
 * @brief Blinky FreeRTOS Example Application main file.
 *
 * This file contains the source code for a sample application using FreeRTOS to blink LEDs.
 *
 */

/*********************************************************************
 * INCLUDES
 */

#include "main.h"

/*********************************************************************
 * CONSTANTS
 */

#define SYSTEM_INDICATOR_LED_INTERVAL     5000     /**< Sensor Contact Detected toggle interval (ms). */
#define APP_TIMER_PRESCALER                  0     /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_OP_QUEUE_SIZE              0     /**< Size of timer operation queues. */
#define OSTIMER_WAIT_FOR_QUEUE               2     /**< Number of ticks to wait for the timer queue to be ready */

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

static TimerHandle_t m_system_indicator_timer;     /**< Definition of system indicator led timer. */
static TaskHandle_t  m_ble_stack_thread;           /**< Definition of BLE stack thread. */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * FUNCTIONS PROTOTYPE
 */

static void clock_initialization();
static void ble_stack_thread(void * arg);
static void timers_init(void);
static void system_indicator_timeout_handler(void * p_context);
static void buttons_leds_init(bool * p_erase_bonds);
static void ble_stack_init(void);
static void device_manager_init(bool erase_bonds);
static void gap_params_init(void);
static void advertising_init(void);
static void advertising_start(void);
static void services_init(void);
static void conn_params_init(void);
static void application_timers_start(void);

/**
  * @brief  Main program
  * @param  None
  * @retval None
  */

int main(void)
{
    clock_initialization();
    // Start execution.
    if(pdPASS != xTaskCreate(ble_stack_thread, "BLE", APP_TASK_BLE_STACK_SIZE,
                             NULL, APP_TASK_BLE_PRIORITY, &m_ble_stack_thread))
    {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }
    
    /* Activate deep sleep mode */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    // Start FreeRTOS scheduler.
    vTaskStartScheduler();

    while (true)
    {
        // FreeRTOS should not be here...
        APP_ERROR_HANDLER(NRF_ERROR_FORBIDDEN);
    }
}

/**@brief Function for initialization oscillators.
 */
static void clock_initialization()
{
    /* Start 16 MHz crystal oscillator */
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART    = 1;

    /* Wait for the external oscillator to start up */
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0)
    {
        // Do nothing.
    }

    /* Start low frequency crystal oscillator for app_timer(used by bsp)*/
    NRF_CLOCK->LFCLKSRC            = (CLOCK_LFCLKSRC_SRC_Xtal << CLOCK_LFCLKSRC_SRC_Pos);
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_LFCLKSTART    = 1;

    while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0)
    {
        // Do nothing.
    }
}

/**@brief Thread for handling the Application's BLE Stack events.
 *
 * @details This thread is responsible for handling BLE Stack events sent from on_ble_evt().
 *
 * @param[in]   arg   Pointer used for passing some arbitrary information (context) from the
 *                    osThreadCreate() call to the thread.
 */
static void ble_stack_thread(void * arg)
{
    bool      erase_bonds;

    UNUSED_PARAMETER(arg);

    // Initialize.
    timers_init();
    buttons_leds_init(&erase_bonds);
    ble_stack_init();
    device_manager_init(erase_bonds);
    gap_params_init();
    advertising_init();
    services_init();
    conn_params_init();

    application_timers_start();
    advertising_start();

    while (1)
    {
        
    }
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
static void timers_init(void)
{
    // Initialize timer module.
    uint32_t err_code = app_timer_init(APP_TIMER_PRESCALER,
                                       APP_TIMER_OP_QUEUE_SIZE, NULL, NULL);
    APP_ERROR_CHECK(err_code);

    // Create timers.
    m_system_indicator_timer = xTimerCreate("LED", SYSTEM_INDICATOR_LED_INTERVAL,
                                 pdTRUE, NULL, system_indicator_timeout_handler);

    /* Error checking */
    if( NULL == m_system_indicator_timer )
    {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }
}

/**@brief Function for initializing buttons and leds.
 *
 * @param[out] p_erase_bonds  Will be true if the clear bonding button was pressed to wake the application up.
 */
static void buttons_leds_init(bool * p_erase_bonds)
{
    bsp_event_t startup_event;

    uint32_t err_code = bsp_init(BSP_INIT_LED | BSP_INIT_BUTTONS,
                                 APP_TIMER_TICKS(100, APP_TIMER_PRESCALER),
                                 bsp_event_handler);
    APP_ERROR_CHECK(err_code);

    err_code = bsp_btn_ble_init(NULL, &startup_event);
    APP_ERROR_CHECK(err_code);

    *p_erase_bonds = (startup_event == BSP_EVENT_CLEAR_BONDING_DATA);
}

/**@brief Function for handling the system indicator timer timeout.
 *
 * @details This function will be called each time the  timer expires.
 *
 * @param[in] p_context   Pointer used for passing some arbitrary information (context) from the
 *                        app_start_timer() call to the timeout handler.
 */
static void system_indicator_timeout_handler(void * p_context)
{
    UNUSED_PARAMETER(p_context);
    LEDS_INVERT(BSP_LED_4_MASK);
}

/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    NRF_RNG->TASKS_START = 1;
}

/**@brief Function for the Device Manager initialization.
 *
 * @param[in] erase_bonds  Indicates whether bonding information should be cleared from
 *                         persistent storage during initialization of the Device Manager.
 */
static void device_manager_init(bool erase_bonds)
{
    uint32_t               err_code;

    // Initialize persistent storage module.
    err_code = pstorage_init();
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void)
{
    
}

/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void)
{
    
}

/**@brief Function for starting advertising.
 */
static void advertising_start(void)
{
    uint32_t err_code;

    err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing services that will be used by the application.
 *
 * @details Initialize the Heart Rate, Battery and Device Information services.
 */
static void services_init(void)
{
    
}

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
    
}

/**@brief Function for starting application timers.
 */
static void application_timers_start(void)
{
    // Start application timers.
    if(pdPASS != xTimerStart(m_system_indicator_timer, OSTIMER_WAIT_FOR_QUEUE))
    {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }
}

/* Used in debug mode for assertions */
void assert_nrf_callback(uint16_t line_num, const uint8_t *file_name)
{
  while(1)
  {
    /* Loop forever */
  }
}

/**
 *@}
 **/
