// Includes
#include <stdio.h>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "motor_controller.hpp"
#include "communication.hpp"

static constexpr char *TAG = "main";

MotorController motor;
Communication comm;

void bump_test(float start, float end);

extern "C" void app_main(void)
{
  comm.init();
  motor.init();
  motor.set_direction(CLOCKWISE);
  //motor.enable_display();
  motor.set_duty_cycle(0.8);
  while (1)
  {
    // motor.pid_speed(24);
    // bump_test(20, 80);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void bump_test(float start, float end)
{
  // // Initial conditions
  // motor.set_dir(CLOCKWISE);
  // motor.set_speed(0);
  // vTaskDelay(1000 / portTICK_PERIOD_MS);
  // motor.set_speed(start);
  // vTaskDelay(3000 / portTICK_PERIOD_MS);

  // // Wait for steady state then bump and monitor
  // motor.enable_monitor();
  // motor.set_speed(end);
  // vTaskDelay(3000 / portTICK_PERIOD_MS);

  // // Wait for steady state and then disable monitoring and revert speed
  // motor.disable_monitor();
}
