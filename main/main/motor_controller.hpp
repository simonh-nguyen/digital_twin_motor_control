#ifndef MOTOR_CONTROLLER_H_
#define MOTOR_CONTROLLER_H_

// Headers
#include <stdio.h>
#include <chrono>
#include <string>
#include <cmath>
#include <vector>
#include <sstream>

#include "configuration.hpp"
#include "communication.hpp"
#include "current_sensor.hpp"
#include "moving_average.hpp"
#include "azure_iot_freertos.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "driver/mcpwm_prelude.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"

using namespace std;

// Global enumerations
enum ControllerMode : int32_t
{
  OFF = 0,
  MANUAL = 1,
  AUTO_POSITION = 2,
  AUTO_VELOCITY = 3,
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
  uint64_t sample_time;
  int32_t actual_direction;
  float duty_cycle_mag;
  float velocity_mag;
  float absolute_position;

  int32_t mode;
  float gain_mag;
  float gain;
  float freq;
  float position_sp;
  float velocity_sp;
  float position_dir;

  uint64_t timestamp;
  int32_t direction;
  float duty_cycle;
  float velocity;
  float position;
  float current;

  // Sample strings
  static constexpr uint16_t VECTOR_SIZE = 500;
  static constexpr uint16_t MIN_STRING_SIZE = VECTOR_SIZE * 1.05;

  uint8_t curr_buffer;
  uint64_t sample_count;

  static constexpr uint32_t timestamp_size = MIN_STRING_SIZE * 14;
  static constexpr uint32_t direction_size = MIN_STRING_SIZE * 8;
  static constexpr uint32_t duty_cycle_size = MIN_STRING_SIZE * 6;
  static constexpr uint32_t velocity_size = MIN_STRING_SIZE * 8;
  static constexpr uint32_t position_size = MIN_STRING_SIZE * 8;
  static constexpr uint32_t current_size = MIN_STRING_SIZE * 9;
  static constexpr uint32_t sample_size = timestamp_size +
                                          direction_size +
                                          duty_cycle_size +
                                          velocity_size +
                                          position_size +
                                          current_size + 83;

  vector<uint64_t> timestamp_vector[2];
  vector<float> gain_vector[2];
  vector<float> duty_cycle_vector[2];
  vector<float> velocity_vector[2];
  vector<float> position_vector[2];
  vector<float> current_vector[2];

  stringstream sample_ss;

  // ESP handles
  mcpwm_cmpr_handle_t cmpr_hdl;
  pcnt_unit_handle_t unit_hdl;

  // System properties
  static constexpr float REDUCTION_RATIO = 65.0; // DC motor's reduction ratio

  static constexpr double VELOCITY_SAMPLE_SIZE = 2.0;  // Amount of counts to sample for velocity
  static constexpr uint8_t VELOCITY_WINDOW_SIZE = 100; // Size of window for velocity moving average
  static constexpr float CALI_FACTOR = 1.03798;        // Calibration factor to align velocity and position with reference

  static constexpr float MIN_DUTY_CYCLE = 0.5; // Scales duty cycle
  static constexpr uint8_t TIMEOUT = 50;       // Timeout before velocity zeros (in ms)

  // PID controller properties
  static constexpr float PID_MAX_OUTPUT = 1.0;   // Maximum PID output
  static constexpr float PID_MIN_OUTPUT = 0.0;   // Minimum PID output
  static constexpr float PID_OSCILLATION = 0.02; // Percent allowed oscillation
  static constexpr uint8_t PID_WINDUP = 25;      // Maximum integral windup

  static constexpr float kp = 0.00544;
  static constexpr float ti = 0.11655;
  static constexpr float td = 0;

  // MCPWM properties
  static constexpr uint32_t TIMER_RES = 80000000; // 80 MHz
  static constexpr uint32_t TIMER_FREQ = 20000;   // 20 kHz
  static constexpr uint32_t TIMER_PERIOD = TIMER_RES / TIMER_FREQ;

  // PCNT properties
  static constexpr int8_t ENCODER_HIGH_LIMIT = VELOCITY_SAMPLE_SIZE;
  static constexpr int8_t ENCODER_LOW_LIMIT = -VELOCITY_SAMPLE_SIZE;
  static constexpr uint16_t ENCODER_GLITCH_NS = 1000; // Glitch filter width in ns

  // Conversion constants
  static constexpr float US_TO_MS = 1000.0;
  static constexpr float US_TO_S = 1000000.0;
  static constexpr float PPUS_TO_RPM = 60 * US_TO_S / (REDUCTION_RATIO * 11.0 * 4.0);
  static constexpr float PULSE_TO_DEG = 360 / (REDUCTION_RATIO * 11.0 * 4.0);

  // PCNT callback
  static bool pcnt_callback(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx);

  // Semaphores
  SemaphoreHandle_t parameter_semaphore;
  SemaphoreHandle_t buffer_semaphore;
  SemaphoreHandle_t comm_semaphore;

  // Update task
  TaskHandle_t update_task_hdl;
  static void update_trampoline(void *arg);
  void update_task();

  // Format task
  TaskHandle_t format_task_hdl;
  static void format_task(void *arg);

  // PID Controller task
  TaskHandle_t pid_task_hdl;
  static void pid_trampoline(void *arg);
  void pid_velocity_task();
  void pid_position_task();

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
  void set_direction(int32_t direction);
  void set_duty_cycle(float duty_cycle);

  void set_mode(int32_t mode);
  void set_gain(float gain);
  void set_frequency(float freq);
  void set_position(float position_sp);
  void set_velocity(float velocity_sp);

  uint64_t get_timestamp();
  int32_t get_direction();
  float get_duty_cycle();
  float get_velocity();
  float get_position();
  float get_current();
  string get_sample_string();
  uint64_t get_sample_count();

  void enable_display();
  void disable_display();
  void enable_communication();
  void disable_communication();

  void format_samples();
};

#endif // MOTOR_CONTROLLER_H_
