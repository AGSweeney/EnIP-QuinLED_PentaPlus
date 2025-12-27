# QuinLED Penta Plus - ESP-IDF Integration Guide

## Overview

The QuinLED Penta Plus is an ESP32-based LED controller board designed for advanced lighting control. It features five independent PWM channels for analog LED control (supporting tunable white and RGB), digital LED support, Ethernet connectivity, OLED display, and multiple input/output capabilities.

## Hardware Specifications

- **Microcontroller**: ESP32
- **Ethernet**: LAN8720 PHY with RMII interface
- **LED Channels**: 5 analog PWM channels + 1 digital LED channel
- **Display**: SPI OLED display
- **Inputs**: 3 button/switch inputs with hardware debounce
- **I2C**: External I2C connector (Stemma QT/Qwiic compatible)
- **Sensors**: Built-in SHT sensor support

## Ethernet Configuration

The board uses a LAN8720 Ethernet PHY connected via RMII interface. For ESP-IDF integration, configure the Ethernet as follows:

```cpp
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "driver/gpio.h"

#define ETH_PHY_ADDR         0
#define ETH_PHY_POWER_PIN    5
#define ETH_PHY_MDC_PIN      23
#define ETH_PHY_MDIO_PIN     18
#define ETH_PHY_TYPE         ETH_PHY_LAN8720
#define ETH_CLK_MODE         ETH_CLOCK_GPIO17_OUT
```

### ESP-IDF Ethernet Initialization

```cpp
eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
phy_config.phy_addr = ETH_PHY_ADDR;
phy_config.reset_gpio_num = -1;

eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
esp32_emac_config.smi_mdc_gpio_num = ETH_PHY_MDC_PIN;
esp32_emac_config.smi_mdio_gpio_num = ETH_PHY_MDIO_PIN;
esp32_emac_config.clock_config.rmii.clock_mode = ETH_CLK_MODE;
esp32_emac_config.clock_config.rmii.clock_gpio = ETH_CLOCK_GPIO17_OUT;

esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
esp_eth_phy_t *phy = esp_eth_phy_new_lan8720(&phy_config);
```

## GPIO Pin Assignments

### LED Control Channels

The board provides five analog PWM channels for LED control:

| Channel | GPIO | Function | Color/Type |
|---------|------|----------|------------|
| L1 | GPIO_33 | PWM Output | Cool White (CW) - ~5000K-6500K |
| L2 | GPIO_32 | PWM Output | Warm White (WW) - ~2700K-3000K |
| L3 | GPIO_12 | PWM Output | Blue (B) - RGB channel |
| L4 | GPIO_4 | PWM Output | Green (G) - RGB channel |
| L5 | GPIO_2 | PWM Output | Red (R) - RGB channel |

**Note**: There is a discrepancy in pin definitions. The `anPentaLEDPins` array references `{14, 13, 12, 4, 2}`, but the actual GPIO mapping shows L1=33, L2=32. Use the GPIO mapping table above for ESP-IDF configuration.

### Digital LED Control

| Function | GPIO | Description |
|----------|------|-------------|
| Digital LED Data | GPIO_5 | Level-shifted data pin for addressable LED strips (WS2812, SK6812, etc.) |
| Digital LED Relay | GPIO_13 | Power control relay for digital LED positive output |

### Button/Input Channels

Three input channels with hardware debounce and pull-up resistors:

| Input | GPIO | Characteristics |
|-------|------|----------------|
| Button_1 | GPIO_36 | Pulled High, Hardware Debounce |
| Button_2 | GPIO_39 | Pulled High, Hardware Debounce |
| Button_3 | GPIO_34 | Pulled High, Hardware Debounce |

**Note**: GPIO_36 and GPIO_39 are input-only pins on ESP32 and cannot be configured as outputs.

### I2C Interface

External I2C connector for Stemma QT/Qwiic compatible devices:

| Signal | GPIO | Function |
|--------|------|----------|
| I2C SDA | GPIO_15 | I2C Data Line |
| I2C SCL | GPIO_16 | I2C Clock Line |

### OLED Display (SPI)

The onboard OLED display uses SPI interface:

| Signal | GPIO | Function |
|--------|------|----------|
| OLED SPI Clock | GPIO_15 | SPI Clock (SCK) |
| OLED SPI Data | GPIO_16 | SPI Data (MOSI) |
| OLED SPI CS | GPIO_27 | Chip Select |
| OLED SPI DC | GPIO_32 | Data/Command |
| OLED SPI Reset | GPIO_33 | Reset |

**Note**: GPIO_15 and GPIO_16 are shared between I2C and OLED SPI. The OLED uses SPI mode, while the external connector uses I2C. Ensure proper configuration to avoid conflicts.

### Sensor Connections

Built-in support for SHT series sensors:

| Signal | GPIO | Function |
|--------|------|----------|
| SHT SDA | GPIO_1 | Sensor I2C Data |
| SHT SCL | GPIO_3 | Sensor I2C Clock |

## ESP-IDF Implementation Notes

### PWM Configuration for LED Channels

Configure LED channels using the LEDC (LED Controller) peripheral:

```cpp
#include "driver/ledc.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_CW         LEDC_CHANNEL_0
#define LEDC_CHANNEL_WW         LEDC_CHANNEL_1
#define LEDC_CHANNEL_B          LEDC_CHANNEL_2
#define LEDC_CHANNEL_G          LEDC_CHANNEL_3
#define LEDC_CHANNEL_R          LEDC_CHANNEL_4
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY          5000

ledc_timer_config_t ledc_timer = {
    .speed_mode       = LEDC_MODE,
    .timer_num         = LEDC_TIMER,
    .duty_resolution  = LEDC_DUTY_RES,
    .freq_hz          = LEDC_FREQUENCY,
    .clk_cfg          = LEDC_AUTO_CLK
};
ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

ledc_channel_config_t ledc_channel_cw = {
    .speed_mode     = LEDC_MODE,
    .channel        = LEDC_CHANNEL_CW,
    .timer_sel      = LEDC_TIMER,
    .intr_type      = LEDC_INTR_DISABLE,
    .gpio_num       = 33,
    .duty           = 0,
    .hpoint         = 0
};
ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_cw));
```

### Button Input Configuration

Configure button inputs with interrupt support:

```cpp
#include "driver/gpio.h"

#define BUTTON_1_GPIO    36
#define BUTTON_2_GPIO    39
#define BUTTON_3_GPIO    34

gpio_config_t io_conf = {
    .intr_type = GPIO_INTR_NEGEDGE,
    .mode = GPIO_MODE_INPUT,
    .pin_bit_mask = (1ULL << BUTTON_1_GPIO) | (1ULL << BUTTON_2_GPIO) | (1ULL << BUTTON_3_GPIO),
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .pull_up_en = GPIO_PULLUP_ENABLE
};
ESP_ERROR_CHECK(gpio_config(&io_conf));
```

### I2C Configuration

Configure I2C for external devices and sensors:

```cpp
#include "driver/i2c_master.h"

#define I2C_MASTER_SCL_IO    15
#define I2C_MASTER_SDA_IO    16
#define I2C_MASTER_FREQ_HZ   100000

i2c_master_bus_config_t i2c_mst_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = I2C_MASTER_SDA_IO,
    .scl_io_num = I2C_MASTER_SCL_IO,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags = {
        .enable_internal_pullup = true,
    },
};
ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &i2c_master_handle));
```

### Digital LED Configuration

For addressable LED strips (WS2812, SK6812, etc.), use the RMT peripheral or dedicated LED strip drivers:

```cpp
#include "driver/rmt_tx.h"

#define DIGITAL_LED_GPIO    5
#define DIGITAL_LED_NUM     60

rmt_channel_handle_t led_chan = NULL;
rmt_tx_channel_config_t tx_chan_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .gpio_num = DIGITAL_LED_GPIO,
    .mem_block_symbols = 64,
    .resolution_hz = 10 * 1000 * 1000,
    .trans_queue_depth = 4,
};
ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));
```

### GPIO Pin Summary

| GPIO | Function | Notes |
|------|----------|-------|
| GPIO_1 | SHT SDA | Sensor I2C Data |
| GPIO_2 | L5 (Red) | PWM Output |
| GPIO_3 | SHT SCL | Sensor I2C Clock |
| GPIO_4 | L4 (Green) | PWM Output |
| GPIO_5 | Digital LED Data | Level-shifted |
| GPIO_12 | L3 (Blue) | PWM Output |
| GPIO_13 | Digital LED Relay | Power control |
| GPIO_15 | I2C SDA / OLED SPI Clock | Shared, configure appropriately |
| GPIO_16 | I2C SCL / OLED SPI Data | Shared, configure appropriately |
| GPIO_17 | ETH Clock Output | Ethernet RMII clock |
| GPIO_18 | ETH MDIO | Ethernet management |
| GPIO_23 | ETH MDC | Ethernet management |
| GPIO_27 | OLED SPI CS | Display chip select |
| GPIO_32 | L2 (Warm White) | PWM Output |
| GPIO_33 | L1 (Cool White) | PWM Output |
| GPIO_34 | Button_3 | Input only, pulled high |
| GPIO_36 | Button_1 | Input only, pulled high |
| GPIO_39 | Button_2 | Input only, pulled high |

## LED Channel Usage

### Tunable White Control

Channels L1 (Cool White) and L2 (Warm White) can be mixed to achieve different color temperatures:
- **Cool White (CW)**: ~5000K-6500K color temperature
- **Warm White (WW)**: ~2700K-3000K color temperature
- **Mixing**: Adjusting the ratio between CW and WW allows fine-tuning of white light color temperature

### RGB Control

Channels L3 (Blue), L4 (Green), and L5 (Red) provide full RGB color control. These can be used independently or combined with the white channels for enhanced color mixing.

## Power Considerations

- The Digital LED Relay (GPIO_13) controls power to the digital LED output
- Ensure proper power supply capacity for all LED channels
- Consider current limitations when driving multiple channels simultaneously

## Boot Considerations

- **GPIO_2**: Must be HIGH during boot to enter normal boot mode
- **GPIO_12**: Must be LOW during boot to avoid flash voltage issues
- Ensure these pins are configured correctly to prevent boot issues

## Development Recommendations

1. **Pin Conflicts**: Be aware that GPIO_15 and GPIO_16 are shared between I2C and OLED SPI. Use only one interface at a time or implement proper multiplexing.

2. **Input-Only Pins**: GPIO_34, GPIO_36, and GPIO_39 are input-only and cannot be used as outputs.

3. **PWM Frequency**: 5kHz is a good starting point for LED PWM, but adjust based on your application requirements and to avoid audible noise.

4. **Ethernet Clock**: GPIO_17 is dedicated to Ethernet clock output and should not be used for other purposes.

5. **Hardware Debounce**: The button inputs have hardware debounce, but additional software debouncing may still be beneficial for reliability.
