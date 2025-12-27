/*
 * Copyright (c) 2025, Adam G. Sweeney <agsweeney@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "opener_api.h"
#include "appcontype.h"
#include "trace.h"
#include "cipidentity.h"
#include "ciptcpipinterface.h"
#include "cipqos.h"
#include "cipstring.h"
#include "ciptypes.h"
#include "typedefs.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "led_strip_encoder.h"

struct netif;

#define DEMO_APP_INPUT_ASSEMBLY_NUM                100
#define DEMO_APP_OUTPUT_ASSEMBLY_NUM               150

#define OUTPUT_ASSEMBLY_SIZE                      11
#define INPUT_ASSEMBLY_SIZE                       14

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_CW         LEDC_CHANNEL_0
#define LEDC_CHANNEL_WW         LEDC_CHANNEL_1
#define LEDC_CHANNEL_B          LEDC_CHANNEL_2
#define LEDC_CHANNEL_G          LEDC_CHANNEL_3
#define LEDC_CHANNEL_R          LEDC_CHANNEL_4
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY          5000

#define LED_GPIO_CW             33
#define LED_GPIO_WW             32
#define LED_GPIO_B              12
#define LED_GPIO_G              4
#define LED_GPIO_R              2

#define LED_OUTPUT_OFFSET_CW    0
#define LED_OUTPUT_OFFSET_WW    1
#define LED_OUTPUT_OFFSET_B     2
#define LED_OUTPUT_OFFSET_G     3
#define LED_OUTPUT_OFFSET_R     4
#define LED_OUTPUT_OFFSET_DIGITAL_COUNT_LOW  5  // Low byte of LED count (little-endian)
#define LED_OUTPUT_OFFSET_DIGITAL_COUNT_HIGH 6  // High byte of LED count (little-endian)
#define LED_OUTPUT_OFFSET_DIGITAL_ENABLE     7
#define LED_OUTPUT_OFFSET_DIGITAL_R          8
#define LED_OUTPUT_OFFSET_DIGITAL_G          9
#define LED_OUTPUT_OFFSET_DIGITAL_B          10

#define DIGITAL_LED_GPIO        5
#define DIGITAL_LED_RELAY_GPIO 13
#define DIGITAL_LED_MAX_COUNT  65535  // 16-bit value (2 bytes)

#define DIGITAL_INPUT_1_GPIO    36
#define DIGITAL_INPUT_2_GPIO    39
#define DIGITAL_INPUT_3_GPIO    34

#define INPUT_OFFSET_DIGITAL_INPUTS  11  // Start at byte 11 for digital inputs

static EipUint8 s_input_assembly_data[INPUT_ASSEMBLY_SIZE];
static EipUint8 s_output_assembly_data[OUTPUT_ASSEMBLY_SIZE];
static bool s_ledc_initialized = false;
static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;
static uint16_t s_digital_led_count = 0;

static void InitializeLEDC(void) {
  if (s_ledc_initialized) {
    return;
  }

  ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_MODE,
    .timer_num = LEDC_TIMER,
    .duty_resolution = LEDC_DUTY_RES,
    .freq_hz = LEDC_FREQUENCY,
    .clk_cfg = LEDC_AUTO_CLK
  };
  esp_err_t err = ledc_timer_config(&ledc_timer);
  if (err != ESP_OK) {
    return;
  }

  ledc_channel_config_t channels[] = {
    {.speed_mode = LEDC_MODE, .channel = LEDC_CHANNEL_CW, .timer_sel = LEDC_TIMER,
     .intr_type = LEDC_INTR_DISABLE, .gpio_num = LED_GPIO_CW, .duty = 0, .hpoint = 0},
    {.speed_mode = LEDC_MODE, .channel = LEDC_CHANNEL_WW, .timer_sel = LEDC_TIMER,
     .intr_type = LEDC_INTR_DISABLE, .gpio_num = LED_GPIO_WW, .duty = 0, .hpoint = 0},
    {.speed_mode = LEDC_MODE, .channel = LEDC_CHANNEL_B, .timer_sel = LEDC_TIMER,
     .intr_type = LEDC_INTR_DISABLE, .gpio_num = LED_GPIO_B, .duty = 0, .hpoint = 0},
    {.speed_mode = LEDC_MODE, .channel = LEDC_CHANNEL_G, .timer_sel = LEDC_TIMER,
     .intr_type = LEDC_INTR_DISABLE, .gpio_num = LED_GPIO_G, .duty = 0, .hpoint = 0},
    {.speed_mode = LEDC_MODE, .channel = LEDC_CHANNEL_R, .timer_sel = LEDC_TIMER,
     .intr_type = LEDC_INTR_DISABLE, .gpio_num = LED_GPIO_R, .duty = 0, .hpoint = 0}
  };

  for (int i = 0; i < 5; i++) {
    err = ledc_channel_config(&channels[i]);
    if (err != ESP_OK) {
      return;
    }
  }

  s_ledc_initialized = true;
}

static void InitializeDigitalLED(void) {
  if (s_led_chan != NULL) {
    return;
  }

  // Configure relay GPIO
  gpio_config_t relay_conf = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_OUTPUT,
    .pin_bit_mask = (1ULL << DIGITAL_LED_RELAY_GPIO),
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .pull_up_en = GPIO_PULLUP_DISABLE,
  };
  gpio_config(&relay_conf);
  gpio_set_level(DIGITAL_LED_RELAY_GPIO, 0); // Start with relay off

  // Configure RMT channel
  rmt_tx_channel_config_t tx_chan_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .gpio_num = DIGITAL_LED_GPIO,
    .mem_block_symbols = 64,
    .resolution_hz = 10 * 1000 * 1000, // 10MHz
    .trans_queue_depth = 4,
  };
  esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &s_led_chan);
  if (err != ESP_OK) {
    return;
  }

  // Create LED strip encoder
  led_strip_encoder_config_t encoder_config = {
    .resolution = 10 * 1000 * 1000, // 10MHz
  };
  err = led_strip_new_encoder(&encoder_config, &s_led_encoder);
  if (err != ESP_OK) {
    rmt_del_channel(s_led_chan);
    s_led_chan = NULL;
    return;
  }

  // Enable RMT channel
  err = rmt_enable(s_led_chan);
  if (err != ESP_OK) {
    rmt_del_channel(s_led_chan);
    s_led_chan = NULL;
    return;
  }
}

static void InitializeDigitalInputs(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << DIGITAL_INPUT_1_GPIO) | (1ULL << DIGITAL_INPUT_2_GPIO) | (1ULL << DIGITAL_INPUT_3_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&io_conf));
}

static void UpdateDigitalInputs(void) {
  int level1 = gpio_get_level(DIGITAL_INPUT_1_GPIO);
  int level2 = gpio_get_level(DIGITAL_INPUT_2_GPIO);
  int level3 = gpio_get_level(DIGITAL_INPUT_3_GPIO);
  
  s_input_assembly_data[INPUT_OFFSET_DIGITAL_INPUTS] = (level1 == 0) ? 1 : 0;
  s_input_assembly_data[INPUT_OFFSET_DIGITAL_INPUTS + 1] = (level2 == 0) ? 1 : 0;
  s_input_assembly_data[INPUT_OFFSET_DIGITAL_INPUTS + 2] = (level3 == 0) ? 1 : 0;
}

static void UpdateDigitalLEDs(void) {
  if (s_led_chan == NULL || s_led_encoder == NULL) {
    return;
  }

  // Read 16-bit LED count (little-endian: low byte first, then high byte)
  uint16_t led_count = s_output_assembly_data[LED_OUTPUT_OFFSET_DIGITAL_COUNT_LOW] |
                       (s_output_assembly_data[LED_OUTPUT_OFFSET_DIGITAL_COUNT_HIGH] << 8);
  uint8_t enable = s_output_assembly_data[LED_OUTPUT_OFFSET_DIGITAL_ENABLE];
  uint8_t r = s_output_assembly_data[LED_OUTPUT_OFFSET_DIGITAL_R];
  uint8_t g = s_output_assembly_data[LED_OUTPUT_OFFSET_DIGITAL_G];
  uint8_t b = s_output_assembly_data[LED_OUTPUT_OFFSET_DIGITAL_B];

  // Control relay
  gpio_set_level(DIGITAL_LED_RELAY_GPIO, (enable != 0) ? 1 : 0);

  if (led_count == 0 || enable == 0) {
    s_digital_led_count = 0;
    return;
  }

  // LED count is already limited to uint16_t range (0-65535)

  // Only update if count changed or color changed
  static uint16_t last_count = 0;
  static uint8_t last_r = 0, last_g = 0, last_b = 0;
  if (led_count == last_count && r == last_r && g == last_g && b == last_b) {
    return; // No change
  }
  last_count = led_count;
  last_r = r;
  last_g = g;
  last_b = b;

  s_digital_led_count = led_count;

  // Prepare LED data (all LEDs same color)
  // WS2812 expects GRB format, 3 bytes per LED
  rmt_transmit_config_t tx_config = {
    .loop_count = 0,
  };

  // Create array for all LEDs (GRB format for WS2812)
  uint8_t *led_data = (uint8_t *)malloc(led_count * 3);
  if (led_data == NULL) {
    return;
  }

  for (int i = 0; i < led_count; i++) {
    led_data[i * 3 + 0] = g; // Green first (WS2812 uses GRB order)
    led_data[i * 3 + 1] = r; // Red second
    led_data[i * 3 + 2] = b; // Blue third
  }

  // Transmit data (3 bytes per LED)
  rmt_transmit(s_led_chan, s_led_encoder, led_data, led_count * 3, &tx_config);
  
  free(led_data);
}

static void UpdateLEDOutputs(void) {
  if (!s_ledc_initialized) {
    return;
  }

  uint32_t max_duty = (1 << LEDC_DUTY_RES) - 1;

  uint32_t duty_cw = (s_output_assembly_data[LED_OUTPUT_OFFSET_CW] * max_duty) / 255;
  uint32_t duty_ww = (s_output_assembly_data[LED_OUTPUT_OFFSET_WW] * max_duty) / 255;
  uint32_t duty_b = (s_output_assembly_data[LED_OUTPUT_OFFSET_B] * max_duty) / 255;
  uint32_t duty_g = (s_output_assembly_data[LED_OUTPUT_OFFSET_G] * max_duty) / 255;
  uint32_t duty_r = (s_output_assembly_data[LED_OUTPUT_OFFSET_R] * max_duty) / 255;

  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_CW, duty_cw);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_CW);

  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_WW, duty_ww);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_WW);

  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_B, duty_b);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_B);

  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_G, duty_g);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_G);

  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_R, duty_r);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_R);
}

EipStatus ApplicationInitialization(void) {
  InitializeLEDC();
  InitializeDigitalLED();
  InitializeDigitalInputs();

  CreateAssemblyObject(DEMO_APP_OUTPUT_ASSEMBLY_NUM, s_output_assembly_data,
                       OUTPUT_ASSEMBLY_SIZE);

  CreateAssemblyObject(DEMO_APP_INPUT_ASSEMBLY_NUM, s_input_assembly_data,
                       INPUT_ASSEMBLY_SIZE);

  ConfigureExclusiveOwnerConnectionPoint(0, DEMO_APP_OUTPUT_ASSEMBLY_NUM,
                                        DEMO_APP_INPUT_ASSEMBLY_NUM, 0);
  ConfigureInputOnlyConnectionPoint(0, DEMO_APP_OUTPUT_ASSEMBLY_NUM,
                                    DEMO_APP_INPUT_ASSEMBLY_NUM, 0);
  ConfigureListenOnlyConnectionPoint(0, DEMO_APP_OUTPUT_ASSEMBLY_NUM,
                                     DEMO_APP_INPUT_ASSEMBLY_NUM, 0);
  CipRunIdleHeaderSetO2T(false);
  CipRunIdleHeaderSetT2O(false);

  return kEipStatusOk;
}

void HandleApplication(void) {
}

void CheckIoConnectionEvent(unsigned int output_assembly_id,
                            unsigned int input_assembly_id,
                            IoConnectionEvent io_connection_event) {
  (void) output_assembly_id;
  (void) input_assembly_id;
  (void) io_connection_event;
}

EipStatus AfterAssemblyDataReceived(CipInstance *instance) {
  if (instance->instance_number == DEMO_APP_OUTPUT_ASSEMBLY_NUM) {
    UpdateLEDOutputs();
    UpdateDigitalLEDs();
    memcpy(s_input_assembly_data, s_output_assembly_data, OUTPUT_ASSEMBLY_SIZE);
    UpdateDigitalInputs();
  }
  return kEipStatusOk;
}

EipBool8 BeforeAssemblyDataSend(CipInstance *instance) {
  if (instance->instance_number == DEMO_APP_INPUT_ASSEMBLY_NUM) {
    UpdateDigitalInputs();
  }
  return true;
}

EipStatus ResetDevice(void) {
  CloseAllConnections();
  CipQosUpdateUsedSetQosValues();
  return kEipStatusOk;
}

EipStatus ResetDeviceToInitialConfiguration(void) {
  g_tcpip.encapsulation_inactivity_timeout = 120;
  CipQosResetAttributesToDefaultValues();
  CloseAllConnections();
  return kEipStatusOk;
}

void* CipCalloc(size_t number_of_elements, size_t size_of_element) {
  return calloc(number_of_elements, size_of_element);
}

void CipFree(void *data) {
  free(data);
}

void RunIdleChanged(EipUint32 run_idle_value) {
  (void) run_idle_value;
}

void QuinLED_EnIP_ApplicationNotifyLinkUp(void) {
}

void QuinLED_EnIP_ApplicationNotifyLinkDown(void) {
}

void QuinLED_EnIP_ApplicationSetActiveNetif(struct netif *netif) {
  (void) netif;
}

#if defined(OPENER_ETHLINK_CNTRS_ENABLE) && 0 != OPENER_ETHLINK_CNTRS_ENABLE
EipStatus EthLnkPreGetCallback(CipInstance *instance,
                               CipAttributeStruct *attribute,
                               CipByte service) {
  (void) instance;
  (void) attribute;
  (void) service;
  return kEipStatusOk;
}

EipStatus EthLnkPostGetCallback(CipInstance *instance,
                              CipAttributeStruct *attribute,
                              CipByte service) {
  (void) instance;
  (void) attribute;
  (void) service;
  return kEipStatusOk;
}
#else
EipStatus EthLnkPreGetCallback(CipInstance *instance,
                               CipAttributeStruct *attribute,
                               CipByte service) {
  (void) instance;
  (void) attribute;
  (void) service;
  return kEipStatusOk;
}

EipStatus EthLnkPostGetCallback(CipInstance *instance,
                                CipAttributeStruct *attribute,
                                CipByte service) {
  (void) instance;
  (void) attribute;
  (void) service;
  return kEipStatusOk;
}
#endif

