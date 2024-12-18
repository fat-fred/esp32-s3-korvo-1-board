/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2023 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "esp_log.h"
#include "audio_mem.h"
#include "audio_volume.h"
#include "periph_button.h"
#include "periph_sdcard.h"
#include "periph_adc_button.h"
#include "board.h"

static const char *TAG = "AUDIO_BOARD";

static audio_board_handle_t board_handle = 0;

audio_board_handle_t audio_board_init(void) {
  if (board_handle) {
    ESP_LOGW(TAG, "The board has already been initialized!");
    return board_handle;
  }
  board_handle = (audio_board_handle_t) audio_calloc(1, sizeof(struct audio_board_handle));
  AUDIO_MEM_CHECK(TAG, board_handle, return NULL);
  board_handle->audio_hal = audio_board_codec_init();
  board_handle->adc_hal = audio_board_adc_init();
  return board_handle;
}

audio_hal_handle_t audio_board_adc_init(void)
{
    ESP_LOGD(TAG, "Initializing the adc");
    audio_hal_codec_config_t audio_codec_cfg = AUDIO_CODEC_ES7210_CONFIG();
    audio_hal_handle_t adc_hal = audio_hal_init(&audio_codec_cfg, &AUDIO_CODEC_ES7210_DEFAULT_HANDLE);
    AUDIO_NULL_CHECK(TAG, adc_hal, return NULL);
    return adc_hal;
}

audio_hal_handle_t audio_board_codec_init(void)
{
    ESP_LOGD(TAG, "Initializing the codec");
    audio_hal_codec_config_t audio_codec_cfg = AUDIO_CODEC_ES8311_CONFIG();
    audio_hal_handle_t codec_hal = audio_hal_init(&audio_codec_cfg, &AUDIO_CODEC_ES8311_DEFAULT_HANDLE);
    AUDIO_NULL_CHECK(TAG, codec_hal, return NULL);
    return codec_hal;
}

esp_err_t audio_board_set_volume(audio_board_handle_t board_handle, int volume) {
  if (!board_handle || !board_handle->audio_hal) {
    ESP_LOGE(TAG, "Invalid board or audio HAL handle");
    return ESP_FAIL;
  }
  return audio_hal_set_volume(board_handle->audio_hal, volume);
}

esp_err_t audio_board_get_volume(audio_board_handle_t board_handle, int *volume) {
  if (!board_handle || !board_handle->audio_hal || !volume) {
    ESP_LOGE(TAG, "Invalid board or audio HAL handle, or null volume pointer");
    return ESP_FAIL;
  }
  return audio_hal_get_volume(board_handle->audio_hal, volume);
}

esp_err_t audio_board_key_init(esp_periph_set_handle_t set)
{
    periph_adc_button_cfg_t adc_btn_cfg = PERIPH_ADC_BUTTON_DEFAULT_CONFIG();
    adc_arr_t adc_btn_tag = ADC_DEFAULT_ARR();
    adc_btn_tag.total_steps = 6;
    adc_btn_tag.adc_ch = ADC1_CHANNEL_4;
    int btn_array[7] = {190, 600, 1000, 1375, 1775, 2195, 3000};
    adc_btn_tag.adc_level_step = btn_array;
    adc_btn_cfg.arr = &adc_btn_tag;
    adc_btn_cfg.arr_size = 1;
    if (audio_mem_spiram_stack_is_enabled()) {
        adc_btn_cfg.task_cfg.ext_stack = true;
    }
    esp_periph_handle_t adc_btn_handle = periph_adc_button_init(&adc_btn_cfg);
    AUDIO_NULL_CHECK(TAG, adc_btn_handle, return ESP_ERR_ADF_MEMORY_LACK);
    return esp_periph_start(set, adc_btn_handle);
}

esp_err_t audio_board_sdcard_init(esp_periph_set_handle_t set, periph_sdcard_mode_t mode) {
  if (mode != SD_MODE_1_LINE && mode != SD_MODE_4_LINE) {
    ESP_LOGE(TAG, "Current board only support 1-line and 4-line SD mode!");
    return ESP_FAIL;
  }
  periph_sdcard_cfg_t sdcard_cfg = {.root = "/sdcard", .card_detect_pin = get_sdcard_intr_gpio(), .mode = mode};

  // Enable SDCard power
  if (get_sdcard_power_ctrl_gpio() >= 0) {
    gpio_config_t gpio_cfg = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << get_sdcard_power_ctrl_gpio()};
    gpio_config(&gpio_cfg);
    gpio_set_level(get_sdcard_power_ctrl_gpio(), 0);
  }

  esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
  esp_err_t ret = esp_periph_start(set, sdcard_handle);
  int retry_time = 5;
  bool mount_flag = false;
  while (retry_time--) {
    if (periph_sdcard_is_mounted(sdcard_handle)) {
      mount_flag = true;
      break;
    } else {
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
  }
  if (mount_flag == false) {
    ESP_LOGE(TAG, "Sdcard mount failed");
    return ESP_FAIL;
  }
  return ret;
}

audio_board_handle_t audio_board_get_handle(void) { return board_handle; }

esp_err_t audio_board_deinit(audio_board_handle_t audio_board) {
  AUDIO_NULL_CHECK(TAG, audio_board, return ESP_FAIL);
  esp_err_t ret = ESP_OK;
  ret |= audio_hal_deinit(audio_board->audio_hal);
  ret |= audio_hal_deinit(audio_board->adc_hal);
  audio_free(audio_board);
  board_handle = NULL;
  return ret;
}
