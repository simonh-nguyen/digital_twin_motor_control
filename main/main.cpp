// Includes
#include <stdio.h>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "motor_controller.hpp"

static constexpr char *TAG = "main";

MotorController motor;

void bump_test(float start, float end);

extern "C" void app_main(void)
{
  motor.init();
  while (1)
  {
    bump_test(50, 60);
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}

void bump_test(float start, float end)
{
  // Initial conditions
  motor.set_dir(CLOCKWISE);
  motor.set_speed(start);
  vTaskDelay(3000 / portTICK_PERIOD_MS);
  // Wait for steady state then bump and monitor
  motor.enable_monitor();
  motor.set_speed(end);
  vTaskDelay(3000 / portTICK_PERIOD_MS);
  // Wait for steady state and then disable monitoring and revert speed
  motor.disable_monitor();
  motor.set_speed(start);
}
