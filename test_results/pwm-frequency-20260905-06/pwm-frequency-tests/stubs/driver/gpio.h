#pragma once
#include <driver/ledc.h>
using gpio_num_t=int;
enum gpio_mode_t { GPIO_MODE_OUTPUT,GPIO_MODE_INPUT };
inline esp_err_t gpio_reset_pin(gpio_num_t pin) {
  fake::pins[pin].attached=false; fake::pins[pin].enabled=false; fake::pins[pin].duty=0; return ESP_OK;
}
inline esp_err_t gpio_set_direction(gpio_num_t pin,gpio_mode_t mode) {
  fake::pins[pin].mode=mode==GPIO_MODE_OUTPUT?OUTPUT:INPUT; return ESP_OK;
}
inline esp_err_t gpio_set_level(gpio_num_t pin,uint32_t level) { fake::pins[pin].level=level; return ESP_OK; }
