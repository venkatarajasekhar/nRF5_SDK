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

#define SYSTEM_INDICATOR_LED_INTERVAL        5000                      /**< Sensor Contact Detected toggle interval (ms). */
#define APP_TIMER_PRESCALER                  portNRF_RTC_PRESCALER     /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_OP_QUEUE_SIZE              0                         /**< Size of timer operation queues. */
#define OSTIMER_WAIT_FOR_QUEUE               2                         /**< Number of ticks to wait for the timer queue to be ready */

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

static TimerHandle_t m_system_indicator_timer;     /**< Definition of system indicator led timer. */
static TaskHandle_t  m_ble_app_thread;           /**< Definition of BLE stack thread. */

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
static void bsp_event_handler(bsp_event_t event);
static void sleep_mode_enter(void);
static void ble_stack_init(struct os_eventq *app_evq);
static void device_manager_init(bool erase_bonds);
static void gap_params_init(void);
static void advertising_init(void);
static void advertising_start(void);
static void services_init(void);
static void conn_params_init(void);
static void application_timers_start(void);
static void system_info_output(void);

/**
  * @brief  Main program
  * @param  None
  * @retval None
  */

int main(void)
{
    clock_initialization();
    system_info_output();
    /* Start execution */
    if(pdPASS != xTaskCreate(ble_stack_thread, "ble_app", APP_TASK_BLE_STACK_SIZE,
                             NULL, APP_TASK_BLE_PRIORITY, &m_ble_app_thread))
    {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }
    
    /* Activate deep sleep mode */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    /* Start FreeRTOS scheduler */
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
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);
    /* Start 16 MHz crystal oscillator */
    nrf_drv_clock_hfclk_request(NULL);
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
    bool             erase_bonds;
	struct os_eventq stack_evq;

    UNUSED_PARAMETER(arg);

    // Initialize.
    timers_init();
    buttons_leds_init(&erase_bonds);
    ble_stack_init(&stack_evq);
    device_manager_init(erase_bonds);
    gap_params_init();
    advertising_init();
    services_init();
    conn_params_init();

    application_timers_start();
    advertising_start();

    while (1)
    {
        vTaskDelay(1000);
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

    err_code = bsp_btn_init(NULL, &startup_event);
    APP_ERROR_CHECK(err_code);

    *p_erase_bonds = (startup_event == BSP_EVENT_CLEAR_BONDING_DATA);
}

/**@brief Function for handling events from the BSP module.
 *
 * @param[in]   event   Event generated by button press.
 */
static void bsp_event_handler(bsp_event_t event)
{
    switch (event)
    {
        case BSP_EVENT_SLEEP:
            sleep_mode_enter();
            break;

        case BSP_EVENT_DISCONNECT:
        case BSP_EVENT_WHITELIST_OFF:
        default:
            break;
    }
}

/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
static void sleep_mode_enter(void)
{
    uint32_t err_code = bsp_indication_set(BSP_INDICATE_IDLE);
    APP_ERROR_CHECK(err_code);

    // Prepare wakeup buttons.
    err_code = bsp_btn_sleep_mode_prepare();
    APP_ERROR_CHECK(err_code);

    // Go to system-off mode (this function will not return; wakeup will cause a reset).
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
    //LEDS_INVERT(BSP_LED_4_MASK);
}

/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(struct os_eventq *app_evq)
{
    int rc;
    /* Set cputime to count at 1 usec increments */
    rc = cputime_init(APP_TASK_CPU_TIMER_PRIORITY);
    ASSERT(rc == OS_OK);

    /* Initialize the statistics package */
    rc = stats_module_init();
    ASSERT(rc == OS_OK);

    /* Initialize the BLE LL */
    rc = ble_ll_init(APP_TASK_LINK_PRIORITY, 7, 260);
    ASSERT(rc == 0);

    /* Initialize the BLE host. */
    rc = ble_hs_init(app_evq, NULL);
    ASSERT(rc == BLE_HS_ENONE);
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

/**@brief Function for printing system infomation.
 */
static void system_info_output(void)
{
    nrf_ic_info_t ic_info;
    uint8_t varient, revision;

    nrf_ic_info_get(&ic_info);
    varient = (SCB->CPUID & SCB_CPUID_VARIANT_Msk) >> SCB_CPUID_VARIANT_Pos;
    revision = (SCB->CPUID & SCB_CPUID_REVISION_Msk) >> SCB_CPUID_REVISION_Pos;

    NRF_LOG_PRINTF("\r\nnRF51822(Rev.%d) Features:\r\n", ic_info.ic_revision);
    NRF_LOG_PRINTF("- ARM Cortex-M0 r%dp%d Core\r\n", varient, revision);
    NRF_LOG_PRINTF("- %dkB Flash + %dkB RAM\r\n", ic_info.flash_size, ic_info.ram_size);
}

/* Used in debug mode for assertions */
void assert_nrf_callback(uint16_t line_num, const uint8_t *file_name)
{
    taskDISABLE_INTERRUPTS();
    NRF_LOG_PRINTF("\r\nAssert failed:\r\n");
    NRF_LOG_PRINTF("File Name:   %s\r\n", file_name);
    NRF_LOG_PRINTF("Line Number: %d\r\n", line_num);

    while(1)
    {
        /* Loop forever */
    }
}

/* Function for processing HardFault exceptions */
void HardFault_process(HardFault_stack_t *p_stack)
{
	NRF_LOG_PRINTF("\r\nIn Hard Fault Handler\r\n");
    NRF_LOG_PRINTF("R0  = 0x%08X\r\n", p_stack->r0);
    NRF_LOG_PRINTF("R1  = 0x%08X\r\n", p_stack->r1);
    NRF_LOG_PRINTF("R2  = 0x%08X\r\n", p_stack->r2);
    NRF_LOG_PRINTF("R12 = 0x%08X\r\n", p_stack->r12);
    NRF_LOG_PRINTF("LR  = 0x%08X\r\n", p_stack->lr);
    NRF_LOG_PRINTF("PC  = 0x%08X\r\n", p_stack->pc);
    NRF_LOG_PRINTF("PSR = 0x%08X\r\n", p_stack->psr);

    while(1)
    {
        /* Loop forever */
    }
}

/* System tick hook function */
void vApplicationTickHook(void)
{
    os_time_advance(1);
}

/**
 *@}
 **/
