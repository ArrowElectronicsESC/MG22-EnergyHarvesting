/***************************************************************************//**
 * @file
 * @brief Application specific overrides of weak functions defined as part of
 * the test application.
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 *******************************************************************************
 *
 * * This software is made for use with the Energy Harvesting reference design.
 * It is based off of the Zigbee Green Power Device example for EFR32MG22
 * developed by Silicon Labs available at
 * https://www.silabs.com/community/wireless/zigbee-and-thread/knowledge-base.
 * entry.html/2020/03/13/green_power_devicefromscratchusingefr32mg22-s6dh
 *
 * This reference design was created by Arrow Electronics Inc. in tandem with
 * Silicon Labs. The use of this software is for the existing reference design
 * for Energy Harvesting. This software can be run on a SLWSTK6021A starter kit
 * with BRD4182A radio boards.
 *
 * Developed by: Gregory Madden, Applications Engineer - Arrow Electronics Inc.
 *
 ******************************************************************************/

#include "gpd-components-common.h"
#include "sl_sleeptimer.h"
#include "em_rmu.h"

// If serial plugin is enabled for debug print
#if defined(EMBER_AF_PLUGIN_SERIAL)
#include EMBER_AF_API_SERIAL
#endif

// If debug print is enabled
#if defined(EMBER_AF_PLUGIN_EMBER_MINIMAL_PRINTF)
#define gpdDebugPrintf(...) emberSerialGuaranteedPrintf(APP_SERIAL, __VA_ARGS__)
#else
#define gpdDebugPrintf(...)
#endif

// Removing CLI for the GPD for power saving
//#define GPD_CLI
#if defined(EMBER_AF_PLUGIN_COMMAND_INTERPRETER2)
#include EMBER_AF_API_COMMAND_INTERPRETER2
  #define COMMAND_READER_INIT() emberCommandReaderInit()
#else
  #define COMMAND_READER_INIT()
#endif

// NV Storage
#if defined EMBER_AF_PLUGIN_NVM3
#include "nvm3.h"
#elif defined EMBER_AF_PLUGIN_PSSTORE
extern void store_init(void);
static uint8_t *p;
static uint8_t length;
static uint8_t flags;
#endif

#define EMBER_GPD_NV_DATA_TAG 0xA9A1

// Using the BRD4182A board, use button 1 as the EM4 wake up port/pin
#define EM4WU_PORT_BTN1              BSP_BUTTON1_PORT
#define EM4WU_PIN_BTN1               BSP_BUTTON1_PIN

// If you are using the WSTK board for development, this must be defined.
// This enables button 0 on the board through PB00 and uses PB03 for wake-up.
// For the reference design, button 0 is routed to PB03 to allow use of EM4WU4.
// To enable this feature, you MUST connect EXP7 to EXP16 on the expansion header.
// See the gpd-configurations.h file to undef WSTK_BOARD for use with reference design.
#if defined WSTK_BOARD
#define EM4WU_PORT_BTN0			EM4WU4_PORT
#define EM4WU_PIN_BTN0			EM4WU4_PIN
#else
#define EM4WU_PORT_BTN0			BSP_BUTTON0_PORT
#define EM4WU_PIN_BTN0			BSP_BUTTON0_PIN
#endif

#if defined _SILICON_LABS_32B_SERIES_2_CONFIG_2   //For 4182, PB1
  // Setting up second EM4 wake up mask for button 1
  #define EM4WU_BASE_SHIFT_MASK		(16)
  #define EM4WU_EM4WUEN_NUM_BTN1    (3)
  #define EM4WU_EM4WUEN_MASK_BTN1   (1 << (EM4WU_EM4WUEN_NUM_BTN1 + EM4WU_BASE_SHIFT_MASK))
  // Setting up second EM4 wake up mask for button 0
  #define EM4WU_EM4WUEN_NUM_BTN0	(4)
  #define EM4WU_EM4WUEN_MASK_BTN0	(1 << (EM4WU_EM4WUEN_NUM_BTN0 + EM4WU_BASE_SHIFT_MASK))
#endif

// LED Indication
#define ACTIVITY_LED BOARDLED1
#define COMMISSIONING_STATE_LED BOARDLED0

// Disabling the LED for power saving
#if defined EMBER_AF_PLUGIN_LED
#define BOARD_LED_ON(led) halSetLed(led)
#define BOARD_LED_OFF(led) halClearLed(led)
#else
#define BOARD_LED_ON(led)
#define BOARD_LED_OFF(led)
#endif

// App button press event types
enum {
  APP_EVENT_ACTION_IDLE = 0,
  APP_EVENT_ACTION_SEND_COMMISSION = 0x01,
  APP_EVENT_ACTION_SEND_DECOMMISSION = 0x02,
  APP_EVENT_ACTION_SEND_TOGGLE = 0x03,
  APP_EVENT_ACTION_SEND_REPORT = 0x04,
  APP_EVENT_ACTION_STEP_DOWN = 0x05,
};
typedef uint8_t GpdAppEventActionType;

#ifndef APP_SINGLE_EVENT_COMMISSIONING_LOOP_TIME_MS
#define APP_SINGLE_EVENT_COMMISSIONING_LOOP_TIME_MS 1000
#endif

#ifndef APP_GPD_REPORTING_TIME_MS
#define APP_GPD_REPORTING_TIME_MS 30000
#endif

// Change this value to edit timer wait until entering EM4 power mode
#if !defined APP_GPD_TIME_TO_ENTER_EM4_MS
#define APP_GPD_TIME_TO_ENTER_EM4_MS 5000
#endif

// Step down command from Zigbee GPD command ID's
// To customize different command, see Table 49 GPDF commands with payload sent by GPD
// https://zigbeealliance.org/wp-content/uploads/2019/11/docs-09-5499-26-batt-zigbee-green-power-specification.pdf
#define GP_CMD_STEP_DOWN	0x33

// This enables the periodic commissioning timer for single event commissioning.
#define PERIODIC_COMMISSION_TIMER


// ----------- GPD application functional blocks ------------------------------
// This implements the following
// -- 1. NVM Storage (NVM3 or PSSTORE)for the application - NVM3 is used.
// -- 2. Bidirectional rx offset and window timing
//       Using the sl_sleeptimer driver.
// -- 3. Application main loop
//       Uses button event to wake up
// -- 4. Application Button to run the main loop and command
//       To send (de)commissioning command and  send a toggle
// -- 5. Application main loop
// ----------- END :GPD application functional blocks -------------------------

// ----------------------------------------------------------------------------
// The following code is organised as per the above application functionality
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// ----------- 1. NVM usage in Application ------------------------------------
// ----------------------------------------------------------------------------

/** @brief This is called by framework to initialise the NVM system
 *
 */
void emberGpdAfPluginNvInitCallback(void)
{
  // Initialise the NV
#if defined EMBER_AF_PLUGIN_PSSTORE
  store_init();
#elif defined EMBER_AF_PLUGIN_NVM3
  // use default NVM space and handle
  Ecode_t status = nvm3_open(nvm3_defaultHandle, nvm3_defaultInit);
  if (status != ECODE_NVM3_OK) {
    // Handle error
  }

  // Do re-packing if needed
  if (nvm3_repackNeeded(nvm3_defaultHandle)) {
    status = nvm3_repack(nvm3_defaultHandle);
    if (status != ECODE_NVM3_OK) {
      // Handle error
    }
  }
#endif
}

/** @brief Called to the application to give a chance to load or store the GPD Context
 *.        in a non volatile context. Thsi can help the application to use any other
 *         non volatile storage.
 *
 * @param nvmData The pointer to the data that needs saving or retrieving to or from
 *                the non-volatile memory.
 * @param sizeOfNvmData The size of the data non-volatile data.
 * @param loadStore indication wether to load or store the context.
 * Ver.: always
 *
 * @return true if application handled it.
 */
bool emberGpdAfPluginNvSaveAndLoadCallback(EmberGpd_t * gpd,
                                           uint8_t * nvmData,
                                           uint8_t sizeOfNvmData,
                                           EmebrGpdNvLoadStore_t loadStore)
{
  if (loadStore == EMEBER_GPD_AF_CALLBACK_LOAD_GPD_FROM_NVM) {
  #if defined EMBER_AF_PLUGIN_PSSTORE
    if (0 != store_read(EMBER_GPD_NV_DATA_TAG, &flags, &length, &p)) {
      // Fresh chip , erase, create a storage with default setting.
      store_eraseall();
      // First write to the NVM shadow so that it updated with default ones
      emberGpdCopyToShadow(gpd);
      // Write the data to NVM
      store_write(EMBER_GPD_NV_DATA_TAG,
                  flags,
                  sizeOfNvmData,
                  (void *)nvmData);
    } else {
      memcpy(nvmData, p, sizeOfNvmData);
    }
  #elif defined EMBER_AF_PLUGIN_NVM3
    uint32_t objectType;
    size_t dataLen;
    if (0 == nvm3_countObjects(nvm3_defaultHandle)
        || (nvm3_getObjectInfo(nvm3_defaultHandle,
                               EMBER_GPD_NV_DATA_TAG,
                               &objectType,
                               &dataLen) == ECODE_NVM3_OK
            && objectType != NVM3_OBJECTTYPE_DATA)) {
      // Fresh chip , erase, create a storage with default setting.
      // Erase all objects and write initial data to NVM3
      nvm3_eraseAll(nvm3_defaultHandle);
      // First write to the NVM shadow so that it updated with default ones
      emberGpdCopyToShadow(gpd);
      // Write the data to NVM
      nvm3_writeData(nvm3_defaultHandle,
                     EMBER_GPD_NV_DATA_TAG,
                     nvmData,
                     sizeOfNvmData);
    } else {
      nvm3_readData(nvm3_defaultHandle,
                    EMBER_GPD_NV_DATA_TAG,
                    nvmData,
                    sizeOfNvmData);
    }
  #endif
  } else if (loadStore == EMEBER_GPD_AF_CALLBACK_STORE_GPD_TO_NVM) {
  #if defined EMBER_AF_PLUGIN_PSSTORE
    store_write(EMBER_GPD_NV_DATA_TAG,
                flags,
                sizeOfNvmData,
                (void *)nvmData);
  #elif defined EMBER_AF_PLUGIN_NVM3
    nvm3_writeData(nvm3_defaultHandle,
                   EMBER_GPD_NV_DATA_TAG,
                   nvmData,
                   sizeOfNvmData);
  #endif
  } else {
    // bad command
  }
  return false;
}
// ----------------------------------------------------------------------------
// ----------- END : NVM usage in Application ---------------------------------
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// 2. Bidirectional rxOffset and rxWindow timing
// ----------------------------------------------------------------------------
// Sleep timer.
static sl_sleeptimer_timer_handle_t le_timer_handle, em4_timer_handle;

static void gpdConfigGpiosForEm4()
{
  // Turn on the clock for the GPIO
  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_Unlock();
  GPIO_PinModeSet(EM4WU_PORT_BTN0, EM4WU_PIN_BTN0, gpioModeInputPullFilter, 1UL);
  GPIO_EM4EnablePinWakeup(EM4WU_EM4WUEN_MASK_BTN0, 0);
  GPIO_PinModeSet(EM4WU_PORT_BTN1, EM4WU_PIN_BTN1, gpioModeInputPullFilter, 1UL);
  GPIO_EM4EnablePinWakeup(EM4WU_EM4WUEN_MASK_BTN1, 0);
  GPIO_IntClear(0xFFFFFFFF);
#if defined _SILICON_LABS_32B_SERIES_2_CONFIG_2
  CMU_ClockSelectSet(cmuClock_SYSCLK, cmuSelect_FSRCO);
#endif
}

// Low Power Mode with option to force EM4 mode.
static void gpdEnterLowPowerMode(bool forceEm4)
{
  if (forceEm4) {
    EMU_EM4Init_TypeDef em4Init = EMU_EM4INIT_DEFAULT;

    em4Init.pinRetentionMode = emuPinRetentionEm4Exit;
    em4Init.em4State = emuEM4Shutoff;
    gpdConfigGpiosForEm4();
    EMU_EM4Init(&em4Init);
    gpdDebugPrintf("EM4\n");
    EMU_EnterEM4();
  } else {
    gpdDebugPrintf("EM2\n");
    EMU_EnterEM2(true);
  }
}

// Sleep timer callbacks
static void leTimeCallback(sl_sleeptimer_timer_handle_t *handle, void *contextData)
{
  sl_sleeptimer_stop_timer(handle);
}

static void appEm4TimerCallback(sl_sleeptimer_timer_handle_t *handle, void *contextData)
{
  gpdEnterLowPowerMode(true);
}

bool emberGpdLeTimerRunning(void)
{
  bool running = false;
  sl_sleeptimer_is_timer_running(&le_timer_handle, &running);
  return running;
}

void emberGpdLeTimerInit(void)
{
  // For Low Energy Timing the sleep timer is used , which uses RTC/RTCC
  // Ensure the clock is enabled.

#if defined _SILICON_LABS_32B_SERIES_2
  // Setting RTCC clock source
  CMU_ClockSelectSet(cmuClock_RTCCCLK, cmuSelect_LFRCO);
#endif
  CMU_ClockEnable(cmuClock_RTCC, true);
}

void emberGpdLoadLeTimer(uint32_t timeInUs)
{
  // in Hz => Time period in micro sec T = 1000000/f
  uint32_t f = sl_sleeptimer_get_timer_frequency();
  // ticks needed = (timeout needed in mico sec)/(T in micro sec) =  ((timeout needed in micro sec) * f)/1000000
  uint32_t tick = (timeInUs * f) / 1000000;
  sl_sleeptimer_restart_timer(&le_timer_handle,
                              tick,
                              leTimeCallback,
                              NULL,
                              0,
                              0);
}

void emberGpdAfPluginSleepCallback(void)
{
  EMU_EnterEM2(true);
}
// ----------------------------------------------------------------------------
// ----------- END : Bidirectional rxOffset and rxWindow timing ---------------
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// 3. Application main loop timing
// ----------------------------------------------------------------------------

static void gpdStartEm4Timer(uint32_t countDownTimeInMs)
{
  sl_sleeptimer_stop_timer(&em4_timer_handle);

  sl_sleeptimer_start_timer_ms(&em4_timer_handle,
                               countDownTimeInMs,
                               appEm4TimerCallback,
                               NULL,
                               0,
                               0);
}

// ----------------------------------------------------------------------------
// ----------- END : Application main loop timing -----------------------------
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// ------------Sending an operational command ---------------------------------
// ----------------------------------------------------------------------------
static void sendToggle(EmberGpd_t * gpd)
{
  uint8_t command[] = { GP_CMD_TOGGLE };
  gpd->rxAfterTx = false;
  emberAfGpdfSend(EMBER_GPD_NWK_FC_FRAME_TYPE_DATA,
                  gpd,
                  command,
                  sizeof(command),
                  EMBER_AF_PLUGIN_APPS_CMD_RESEND_NUMBER);
}

static void sendStepDown(EmberGpd_t * gpd)
{
	uint8_t command[] = { GP_CMD_STEP_DOWN };
	gpd->rxAfterTx = false;
	emberAfGpdfSend(EMBER_GPD_NWK_FC_FRAME_TYPE_DATA,
					gpd,
					command,
					sizeof(command),
					EMBER_AF_PLUGIN_APPS_CMD_RESEND_NUMBER);
}
// ----------------------------------------------------------------------------
// ------------ END : Sending an operational command --------------------------
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// -------------- 4. Application Event ----------------------------------------
// ----------------------------------------------------------------------------
static GpdAppEventActionType appAction;

#if defined(EMBER_AF_PLUGIN_COMMAND_INTERPRETER2)
static bool sleepy = false;
void emberGpdAfCliSwitchToggle(void)
{
  appAction = APP_EVENT_ACTION_SEND_TOGGLE;
}
void emberGpdAfCliSwitchSleep(void)
{
  // enable sleep
  sleepy = true;
}
#else
// Setting low power EM4 mode to true
static bool sleepy = true;
#endif

static sl_sleeptimer_timer_handle_t app_single_event_commission;
static void processAppEvent(EmberGpd_t * gpd, GpdAppEventActionType *appAction)
{
  if (*appAction != APP_EVENT_ACTION_IDLE) {
    BOARD_LED_ON(ACTIVITY_LED);
    if (sleepy) {
      gpdStartEm4Timer(APP_GPD_TIME_TO_ENTER_EM4_MS);
    }

    if (*appAction == APP_EVENT_ACTION_SEND_DECOMMISSION) {
      emberGpdAfPluginDeCommission(gpd);
      emberGpdStoreSecDataToNV(gpd);
      gpdDebugPrintf("Decomm Cmd : ");
    } else if (*appAction == APP_EVENT_ACTION_SEND_COMMISSION) {
      emberGpdAfPluginCommission(gpd);
      emberGpdStoreSecDataToNV(gpd);
      gpdDebugPrintf("Comm. Cmd : ");
    } else if (*appAction == APP_EVENT_ACTION_SEND_TOGGLE) {
      sendToggle(gpd);
      emberGpdStoreSecDataToNV(gpd);
      gpdDebugPrintf("Toggle Cmd : ");
    } else if (*appAction == APP_EVENT_ACTION_STEP_DOWN) {
    	sendStepDown(gpd);
    	emberGpdStoreSecDataToNV(gpd);
    	gpdDebugPrintf("Step-Down Cmd: ")
    }
    gpdDebugPrintf("Comm. State :%d\n", gpd->gpdState);
    *appAction = APP_EVENT_ACTION_IDLE;
    BOARD_LED_OFF(ACTIVITY_LED);
  }
}
static void appSingleEventCommissionTimer(sl_sleeptimer_timer_handle_t *handle, void *contextData)
{
  EmberGpd_t * gpd = emberGpdGetGpd();
  if (gpd->gpdState < EMBER_GPD_APP_STATE_OPERATIONAL) {
    appAction = APP_EVENT_ACTION_SEND_COMMISSION;
  } else {
    sl_sleeptimer_stop_timer(handle);
  }
}
// Application Commissioning that completes all the states of the commissioning
void emberGpdAppSingleEventCommission(void)
{
  sl_sleeptimer_restart_periodic_timer_ms(&app_single_event_commission,
                                          APP_SINGLE_EVENT_COMMISSIONING_LOOP_TIME_MS,
                                          appSingleEventCommissionTimer,
                                          NULL,
                                          0,
                                          0);
}
// A button release timeout handler for decommissioning with button 0
static sl_sleeptimer_timer_handle_t button_release_timer_handle;
static void buttonReleaseTimeout(sl_sleeptimer_timer_handle_t *handle, void *contextData)
{
  sl_sleeptimer_stop_timer(handle);
  appAction = APP_EVENT_ACTION_SEND_DECOMMISSION;
}

// Adding debounce for button presses. The functionality comes from button.c
//DEBOUNCE operation is based upon the theory that when multiple reads in a row
//return the same value, we have passed any debounce created by the mechanical
//action of a button.  The define "DEBOUNCE" says how many reads in a row
//should return the same value.  The value '5', below, is the recommended value
//since this should require the signal to have stabalized for approximately
//100us which should be much longer than any debounce action.
//Uncomment the following line to enable software debounce operation:
#define DEBOUNCE 20
uint8_t buttonDebounce(uint8_t pin)
{
  uint8_t buttonStateNow;
  #if (DEBOUNCE > 0)
  uint8_t buttonStatePrev;
  uint32_t debounce;
  #endif //(DEBOUNCE > 0)

  buttonStateNow = halButtonPinState(pin);
  #if (DEBOUNCE > 0)
  //read button until get "DEBOUNCE" number of consistent readings
  for ( debounce = 0;
        debounce < DEBOUNCE;
        debounce = (buttonStateNow == buttonStatePrev) ? debounce + 1 : 0 ) {
    buttonStatePrev = buttonStateNow;
    buttonStateNow = halButtonPinState(pin);
  }
  #endif //(DEBOUNCE > 0)
  return buttonStateNow;
}
// ----------------------------------------------------------------------------
// ------------ END : Application events and actions --------------------------
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// -------------- 4. Application Button ---------------------------------------
// ----------------------------------------------------------------------------
#if defined EMBER_AF_PLUGIN_BUTTON
#if BSP_BUTTON_COUNT == 2
// This function handles all button interrupts
void halButtonIsr(uint8_t button, uint8_t state)
{
  EmberGpd_t * gpd = emberGpdGetGpd();
//  uint8_t button0State = halButtonState(BSP_BUTTON0_PIN);
//  uint8_t button1State = halButtonState(BSP_BUTTON1_PIN);
    uint8_t button0State = buttonDebounce(BSP_BUTTON0_PIN);
    uint8_t button1State = buttonDebounce(BSP_BUTTON1_PIN);

  if (state == BUTTON_PRESSED)
  {
	  // If the GPD is not commissioned, button 0 will handle commission requests
	  // To enable single event commissioning (press button 0 once), enable the PERIODIC_COMMISSION_TIMER.
	  // To enable the 4 button press, remove the PERIODIC_COMMISSION_TIMER define.
	  if (gpd->gpdState < EMBER_GPD_APP_STATE_OPERATIONAL) {
		  if (button0State == BUTTON_PRESSED) {
			#if defined PERIODIC_COMMISSION_TIMER
			  sl_sleeptimer_restart_periodic_timer_ms(&app_single_event_commission,
			                                            APP_SINGLE_EVENT_COMMISSIONING_LOOP_TIME_MS,
			                                            appSingleEventCommissionTimer,
			                                            NULL,
			                                            0,
			                                            0);
			#else
			  // Must press button 0 four times with 1 second wait in-between each press to handle commission process
			  appAction = APP_EVENT_ACTION_SEND_COMMISSION;
			#endif
		  }
	  // If GPD is commissioned, the following function can be used on button 0
	  // Allows for button 1 and 0 to be pressed at same time but button 0 function will continue
	  } else if (button0State == BUTTON_PRESSED || (button0State == BUTTON_PRESSED && button1State == BUTTON_PRESSED)) {
		  // Button 0 must be pressed for 3 seconds to decommission
		  sl_sleeptimer_restart_timer_ms(&button_release_timer_handle,
		  											 3000,
		  											 buttonReleaseTimeout,
		  											 NULL,
		  											 0,
		  											 0);
		  // Pressing (not holding) button 0 sends this custom command
		  // Custom command currently is STEP_DOWN as defined in ZPGD specs
		  appAction = APP_EVENT_ACTION_STEP_DOWN;

	  // Only toggle if just button 1 is pressed
	  } else if (button1State == BUTTON_PRESSED && button0State != BUTTON_PRESSED) {
		  // Button 1 sends a toggle command with each press
		appAction = APP_EVENT_ACTION_SEND_TOGGLE;
	  }

	  } else {
		  // If button 0 has been pressed for 3 seconds, release timer handle
		  // Must wake up from EM4 first to handle 3 second timer
		  sl_sleeptimer_stop_timer(&button_release_timer_handle);
	  }
}
#endif
#endif
// ----------------------------------------------------------------------------
// ----------- END : Application Button ---------------------------------------
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// 5. Application main state machine.
// ----------------------------------------------------------------------------
/** @brief Called by framework from the application main enry to inform the user
 * as the first call to the main.
 *
 */
void emberGpdAfPluginMainCallback(EmberGpd_t * gpd)
{
#if defined(EMBER_AF_PLUGIN_SERIAL)
  emberSerialInit(BSP_SERIAL_APP_PORT, HAL_SERIAL_APP_BAUD_RATE, 0, 1);
  emberSerialGuaranteedPrintf(BSP_SERIAL_APP_PORT, "Reset info: 0x%x (%p)\n",
                              halGetResetInfo(),
                              halGetResetString());
#if defined(EXTENDED_RESET_INFO)
  emberSerialGuaranteedPrintf(BSP_SERIAL_APP_PORT, "Extended Reset info: 0x%2X (%p)",
                              halGetExtendedResetInfo(),
                              halGetExtendedResetString());

  if (halResetWasCrash()) {
    halPrintCrashSummary(serialPort);
    halPrintCrashDetails(serialPort);
    halPrintCrashData(serialPort);
  }
#endif
#endif

  // Initialise Command Interpreter if enabled
  COMMAND_READER_INIT();

  // Initialise timer for application state machine with sleep consideration
  sl_status_t initStatus = sl_sleeptimer_init();
  // Trap if the sleep timer initialisation fails
  while (initStatus != SL_STATUS_OK) ;

  gpdDebugPrintf("GPD Main\n");
  while (true) {
    // Periodically reset the watch dog.
    halResetWatchdog();

    // GPD state LED indication if enabled.
    if (gpd->gpdState < EMBER_GPD_APP_STATE_OPERATIONAL) {
      BOARD_LED_ON(COMMISSIONING_STATE_LED);
    } else {
      BOARD_LED_OFF(COMMISSIONING_STATE_LED);
    }

    // Process CLI
#if defined(EMBER_AF_PLUGIN_COMMAND_INTERPRETER2)
    if (emberProcessCommandInput(APP_SERIAL)) {
      gpdDebugPrintf(EMBER_AF_DEVICE_NAME ">");
    }
#endif
    // Process application actions
    if (appAction != APP_EVENT_ACTION_IDLE) {
      processAppEvent(gpd, &appAction);
    }

    // Enter sleep in sleepy mode, the wake up is on button activation or
    // or when periodic timer expires.
    if (sleepy) {
      gpdStartEm4Timer(APP_GPD_TIME_TO_ENTER_EM4_MS);
      gpdEnterLowPowerMode(false);
    }
  }
}
// ----------------------------------------------------------------------------
// ------------ END : Application main loop -----------------------------------
// ----------------------------------------------------------------------------
/** @brief Called from the imcomming command handler context for all the incoming
 *         command before the command handler handles it. based on the return code
 *         of this callback the internal command handler skips the processing.
 *
 * @param gpdCommand CommandId.
 * @param length length of the command
 * @param commandPayload The pointer to the commissioning reply payload.
 * Ver.: always
 *
 * @return true if application handled it.
 */
bool emberGpdAfPluginIncomingCommandCallback(uint8_t gpdCommand,
                                             uint8_t length,
                                             uint8_t * commandPayload)
{
  gpdDebugPrintf("RX: cmd=%x len=%x payload[", gpdCommand, length);
  if (commandPayload != NULL
      && length > 0
      && length < 0xFE) {
    for (int i = 0; i < length; i++) {
      gpdDebugPrintf("%x", commandPayload[i]);
    }
  }
  gpdDebugPrintf("]\n");
  return false;
}

//Provide a stub
void emberDebugAssert(const char * filename, int linenumber)
{
  gpdDebugPrintf("Assert file: %s Line:%d\n", filename, linenumber);
}

// Provide a stub for Series 1
#if !defined (_SILICON_LABS_32B_SERIES_2)
void emRadioSleep(void)
{
}
#endif // !defined (_SILICON_LABS_32B_SERIES_2) // emRadioSleep
