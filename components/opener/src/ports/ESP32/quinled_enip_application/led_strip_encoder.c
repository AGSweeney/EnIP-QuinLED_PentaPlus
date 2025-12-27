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

#include "led_strip_encoder.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include <string.h>

// WS2812 timing parameters (in nanoseconds)
#define WS2812_T0H_NS   (350)   // 0 bit high time
#define WS2812_T0L_NS   (1000)  // 0 bit low time
#define WS2812_T1H_NS   (1000)  // 1 bit high time
#define WS2812_T1L_NS   (350)   // 1 bit low time
#define WS2812_RES_NS   (280)   // Reset time (minimum 50us, but we use shorter for efficiency)

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t copy_encoder;
    int state;
    rmt_symbol_word_t reset_symbol;
} rmt_led_strip_encoder_t;

static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                   const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state) {
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encode_state_t session_state = 0;
    rmt_encode_state_t state = 0;
    size_t encoded_symbols = 0;
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;

    switch (led_encoder->state) {
    case 0: // send RGB data
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1; // switch to send reset code
        } else {
            state = session_state; // propagate partial state
        }
        break;
    case 1: // send reset code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_symbol,
                                                sizeof(led_encoder->reset_symbol), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 0; // back to send RGB data
            state |= RMT_ENCODING_COMPLETE;
        } else {
            state = session_state; // propagate partial state
        }
        break;
    }
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder) {
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder) {
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = 0;
    return ESP_OK;
}

esp_err_t led_strip_new_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder) {
    esp_err_t ret = ESP_OK;
    rmt_led_strip_encoder_t *led_encoder = NULL;
    rmt_bytes_encoder_config_t bytes_encoder_config;
    rmt_copy_encoder_config_t copy_encoder_config;

    led_encoder = calloc(1, sizeof(rmt_led_strip_encoder_t));
    if (led_encoder == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Configure bytes encoder for RGB data
    bytes_encoder_config.bit0 = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = (uint32_t)(config->resolution * WS2812_T0H_NS / 1e9),
        .level1 = 0,
        .duration1 = (uint32_t)(config->resolution * WS2812_T0L_NS / 1e9),
    };
    bytes_encoder_config.bit1 = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = (uint32_t)(config->resolution * WS2812_T1H_NS / 1e9),
        .level1 = 0,
        .duration1 = (uint32_t)(config->resolution * WS2812_T1L_NS / 1e9),
    };
    bytes_encoder_config.flags.msb_first = 1; // WS2812 sends MSB first
    ret = rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder);
    if (ret != ESP_OK) {
        goto exit;
    }

    // Configure copy encoder for reset code
    ret = rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder);
    if (ret != ESP_OK) {
        goto exit;
    }

    // Create reset symbol (low for reset time)
    led_encoder->reset_symbol = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = (uint32_t)(config->resolution * WS2812_RES_NS / 1e9),
        .level1 = 0,
        .duration1 = 0,
    };

    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;
    led_encoder->state = 0;

    *ret_encoder = &led_encoder->base;
    return ESP_OK;

exit:
    if (led_encoder) {
        if (led_encoder->bytes_encoder) {
            rmt_del_encoder(led_encoder->bytes_encoder);
        }
        if (led_encoder->copy_encoder) {
            rmt_del_encoder(led_encoder->copy_encoder);
        }
        free(led_encoder);
    }
    return ret;
}

