#ifndef MOTOR_CONTROLLER_H_
#define MOTOR_CONTROLLER_H_

// Headers
#include <stdio.h>
#include <cmath>
#include <string.h>

#include "configuration.hpp"
#include "communication.hpp"
#include "current_sensor.hpp"
#include "moving_average.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "driver/mcpwm_prelude.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"

// Global enumerations
enum ControllerMode : int32_t
{
  MANUAL = -1,
  STOP = 0,
  AUTO = 1,
};

enum MotorDirection : int32_t
{
  COUNTERCLOCKWISE = -1,
  CLOCKWISE = 1
};

class MotorController
{
private:
  // Class variables
  uint64_t time_start;
  uint64_t time_sample;
  int32_t mode;
  double set_point;

  uint64_t timestamp;
  int32_t direction;
  double duty_cycle;
  double raw_velocity;
  double velocity;
  double position;
  double current;

  // ESP handles
  mcpwm_cmpr_handle_t cmpr_hdl;
  pcnt_unit_handle_t unit_hdl;

  // Semaphores
  SemaphoreHandle_t data_semaphore;

  // System properties
  static constexpr double REDUCTION_RATIO = 200.0; // DC motor's reduction ratio

  static constexpr uint16_t SAMPLE_SIZE = 12;          // Amount of counts to sample for velocity
  static constexpr uint64_t VELOCITY_WINDOW_SIZE = 10; // Size of window for velocity moving average
  static constexpr double CALI_FACTOR = 1.0379773437;  // Calibration factor to align velocity and position with reference

  static constexpr double MIN_DUTY_CYCLE = 0.5; // Scales duty cycle
  static constexpr uint64_t TIMEOUT = 50;       // Timeout before velocity zeros (in ms)

  // PID controller properties
  static constexpr double PID_MAX_OUTPUT = 1.0;   // Maximum PID output
  static constexpr double PID_MIN_OUTPUT = 0.0;   // Minimum PID output
  static constexpr double PID_OSCILLATION = 0.02; // Percent allowed oscillation
  static constexpr uint8_t PID_WINDUP = 1;        // Maximum integral windup

  static constexpr double kp = 0.034755;
  static constexpr double ti = 0.039281;
  static constexpr double td = 0.0029011;

  // MCPWM properties
  static constexpr uint32_t TIMER_RES = 80000000; // 80 MHz
  static constexpr uint32_t TIMER_FREQ = 20000;   // 20 kHz
  static constexpr uint32_t TIMER_PERIOD = TIMER_RES / TIMER_FREQ;

  // PCNT properties
  static constexpr int16_t ENCODER_HIGH_LIMIT = SAMPLE_SIZE;
  static constexpr int16_t ENCODER_LOW_LIMIT = -SAMPLE_SIZE;
  static constexpr int16_t ENCODER_GLITCH_NS = 1000; // Glitch filter width in ns

  // Conversion constants
  static constexpr double US_TO_MS = 1000.0;
  static constexpr double US_TO_S = 1000000.0;
  static constexpr double PPUS_TO_RPM = 60 * US_TO_S / (REDUCTION_RATIO * 11.0 * 4.0);
  static constexpr double PULSE_TO_DEG = 360 / (REDUCTION_RATIO * 11.0 * 4.0);

  // PCNT callback
  static bool pcnt_callback(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx);

  // Update task
  TaskHandle_t update_task_hdl;
  static void update_trampoline(void *arg);
  void update_task();

  // PID Controller task
  TaskHandle_t pid_task_hdl;
  static void pid_trampoline(void *arg);
  void pid_task();

  // TX Data task
  TaskHandle_t tx_data_task_hdl;
  static void tx_data_task(void *arg);

  // Display task
  TaskHandle_t display_task_hdl;
  static void display_task(void *arg);

public:
  MotorController();

  void init();
  void stop_motor();

  // Accessor and mutator functions
  void set_mode(int32_t mode);
  void set_direction(int32_t direction);
  void set_duty_cycle(double duty_cycle);
  void set_velocity(double set_point);
  void set_position(double position);

  uint64_t get_timestamp();
  double get_duty_cycle();
  int32_t get_direction();
  double get_velocity();
  double get_position();
  double get_current();

  void enable_display();
  void disable_display();
};

#endif // MOTOR_CONTROLLER_H_
