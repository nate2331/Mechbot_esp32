#pragma once
#include <Arduino.h>
using esp_err_t=int;
constexpr esp_err_t ESP_OK=0, ESP_FAIL=-1;
enum ledc_mode_t { LEDC_HIGH_SPEED_MODE,LEDC_LOW_SPEED_MODE };
enum ledc_timer_t { LEDC_TIMER_0,LEDC_TIMER_1,LEDC_TIMER_2,LEDC_TIMER_3 };
enum ledc_timer_bit_t { LEDC_TIMER_8_BIT=8,LEDC_TIMER_9_BIT=9,LEDC_TIMER_10_BIT=10 };
enum ledc_channel_t { LEDC_CHANNEL_0,LEDC_CHANNEL_1,LEDC_CHANNEL_2,LEDC_CHANNEL_3,
  LEDC_CHANNEL_4,LEDC_CHANNEL_5,LEDC_CHANNEL_6,LEDC_CHANNEL_7 };
enum ledc_intr_type_t { LEDC_INTR_DISABLE };
struct ledc_timer_config_t {
  ledc_mode_t speed_mode{}; ledc_timer_bit_t duty_resolution{};
  ledc_timer_t timer_num{}; uint32_t freq_hz{}; ledc_clk_cfg_t clk_cfg{}; bool deconfigure{};
};
struct ledc_channel_config_t {
  int gpio_num{}; ledc_mode_t speed_mode{}; ledc_channel_t channel{};
  ledc_intr_type_t intr_type{}; ledc_timer_t timer_sel{}; uint32_t duty{}; int hpoint{};
  struct { unsigned output_invert:1; } flags{};
};
inline esp_err_t ledc_timer_config(const ledc_timer_config_t *c) {
  ++fake::timerCalls;
  if(!fake::outputsLow()) ++fake::unsafeTimerChanges;
  if(fake::timerCalls==fake::failTimerAt || fake::failClock) return ESP_FAIL;
  fake::timerHz=c->freq_hz; fake::timerBits=c->duty_resolution; fake::clock=c->clk_cfg;
  for(int pin:fake::channelPins) if(pin>=0) { fake::pins[pin].hz=fake::timerHz; fake::pins[pin].bits=fake::timerBits; }
  return ESP_OK;
}
inline esp_err_t ledc_channel_config(const ledc_channel_config_t *c) {
  if(++fake::channelCalls==fake::failChannelAt) return ESP_FAIL;
  const int pin=c->gpio_num;
  fake::channelPins[c->channel]=pin; fake::pendingDuty[c->channel]=c->duty;
  auto &p=fake::pins[pin]; p.attached=true; p.enabled=true; p.duty=c->duty;
  p.hz=fake::timerHz; p.bits=fake::timerBits; p.level=0;
  fake::writes.push_back({fake::now,uint8_t(pin),c->duty,true});
  return ESP_OK;
}
inline esp_err_t ledc_stop(ledc_mode_t,ledc_channel_t c,uint32_t idle) {
  if(++fake::stopCalls==fake::failStopAt) return ESP_FAIL;
  const int pin=fake::channelPins[c]; if(pin<0) return ESP_FAIL;
  // IDF stop disables output, preserving the programmed duty and GPIO mapping.
  fake::pins[pin].enabled=false; fake::pins[pin].level=idle;
  return ESP_OK;
}
inline esp_err_t ledc_set_duty(ledc_mode_t,ledc_channel_t c,uint32_t duty) {
  const bool ok=++fake::setDutyCalls!=fake::failSetDutyAt &&
    !(fake::failPositiveWrite && duty>0) && fake::channelPins[c]>=0;
  if(ok) fake::pendingDuty[c]=duty;
  return ok?ESP_OK:ESP_FAIL;
}
inline esp_err_t ledc_update_duty(ledc_mode_t,ledc_channel_t c) {
  const int pin=fake::channelPins[c];
  const bool ok=++fake::updateCalls!=fake::failUpdateAt && pin>=0;
  if(pin>=0) fake::writes.push_back({fake::now,uint8_t(pin),fake::pendingDuty[c],ok});
  if(ok) { fake::pins[pin].duty=fake::pendingDuty[c]; fake::pins[pin].enabled=true; }
  return ok?ESP_OK:ESP_FAIL;
}
inline uint32_t ledc_get_freq(ledc_mode_t,ledc_timer_t) {
  return fake::readbackOverride>=0?uint32_t(fake::readbackOverride):fake::timerHz;
}
