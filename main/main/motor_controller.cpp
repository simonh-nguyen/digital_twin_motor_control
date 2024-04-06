// Includes
#include "motor_controller.hpp"

static constexpr char *TAG = "Motor";

static MotorController *motor_obj;
static Communication comm;
static CurrentSensor curr_sen;

MotorController::MotorController()
{
  motor_obj = this;

  sample_time = 0;
  raw_velocity = 0;

  mode = STOP;
  set_point = 0;

  timestamp = 0;
  direction = 0;
  duty_cycle = 0;
  velocity = 0;
  position = 0;
  current = 0;

  curr_buffer = 0;
  buffer_ready = false;
  sample_string.reserve(50000);

  cmpr_hdl = nullptr;
  unit_hdl = nullptr;

  update_task_hdl = NULL;
  format_task_hdl = NULL;
  pid_task_hdl = NULL;
  tx_data_task_hdl = NULL;
  display_task_hdl = NULL;
}

void MotorController::init()
{
  ESP_LOGI(TAG, "Setting up output to ENA.");

  mcpwm_timer_handle_t timer_hdl = nullptr;
  mcpwm_timer_config_t timer_config = {
      .group_id = 0,
      .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
      .resolution_hz = TIMER_RES,
      .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
      .period_ticks = TIMER_PERIOD,
  };
  ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer_hdl));

  mcpwm_oper_handle_t oper_hdl = nullptr;
  mcpwm_operator_config_t oper_config = {
      .group_id = 0,
  };
  ESP_ERROR_CHECK(mcpwm_new_operator(&oper_config, &oper_hdl));
  ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper_hdl, timer_hdl));

  mcpwm_comparator_config_t cmpr_config = {
      .flags = {
          .update_cmp_on_tez = true,
      },
  };
  ESP_ERROR_CHECK(mcpwm_new_comparator(oper_hdl, &cmpr_config, &cmpr_hdl));

  mcpwm_gen_handle_t gen_hdl = nullptr;
  mcpwm_generator_config_t gen_config = {
      .gen_gpio_num = GPIO_ENA,
      .flags = {
          .pull_down = 1,
      },
  };
  ESP_ERROR_CHECK(mcpwm_new_generator(oper_hdl, &gen_config, &gen_hdl));

  ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpr_hdl, TIMER_PERIOD * MIN_DUTY_CYCLE));
  ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(gen_hdl, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
  ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(gen_hdl, MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmpr_hdl, MCPWM_GEN_ACTION_LOW)));

  ESP_ERROR_CHECK(mcpwm_timer_enable(timer_hdl));
  ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer_hdl, MCPWM_TIMER_START_NO_STOP));

  ESP_LOGI(TAG, "Setting up outputs to IN1 and IN2.");
  gpio_config_t output_config = {
      .pin_bit_mask = ((1ULL << GPIO_IN1) | (1ULL << GPIO_IN2)),
      .mode = GPIO_MODE_OUTPUT,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
  };
  gpio_config(&output_config);
  gpio_set_level(GPIO_IN1, 0);
  gpio_set_level(GPIO_IN2, 0);

  ESP_LOGI(TAG, "Setting up inputs for encoder A and B.");
  pcnt_unit_config_t unit_config = {
      .low_limit = ENCODER_LOW_LIMIT,
      .high_limit = ENCODER_HIGH_LIMIT,
      .flags = {
          .accum_count = 1,
      },
  };
  ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &unit_hdl));

  pcnt_glitch_filter_config_t filter_config = {
      .max_glitch_ns = ENCODER_GLITCH_NS,
  };
  ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unit_hdl, &filter_config));

  pcnt_channel_handle_t channel_a_hdl = nullptr;
  pcnt_chan_config_t channel_a_config = {
      .edge_gpio_num = GPIO_C1,
      .level_gpio_num = GPIO_C2,
  };
  ESP_ERROR_CHECK(pcnt_new_channel(unit_hdl, &channel_a_config, &channel_a_hdl));
  pcnt_channel_handle_t channel_b_hdl = nullptr;
  pcnt_chan_config_t channel_b_config = {
      .edge_gpio_num = GPIO_C2,
      .level_gpio_num = GPIO_C1,
  };
  ESP_ERROR_CHECK(pcnt_new_channel(unit_hdl, &channel_b_config, &channel_b_hdl));

  ESP_ERROR_CHECK(pcnt_channel_set_edge_action(channel_a_hdl, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
  ESP_ERROR_CHECK(pcnt_channel_set_level_action(channel_a_hdl, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
  ESP_ERROR_CHECK(pcnt_channel_set_edge_action(channel_b_hdl, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
  ESP_ERROR_CHECK(pcnt_channel_set_level_action(channel_b_hdl, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

  ESP_ERROR_CHECK(pcnt_unit_add_watch_point(unit_hdl, ENCODER_LOW_LIMIT));
  ESP_ERROR_CHECK(pcnt_unit_add_watch_point(unit_hdl, ENCODER_HIGH_LIMIT));

  pcnt_event_callbacks_t pcnt_cbs = {
      .on_reach = pcnt_callback,
  };
  QueueHandle_t queue = xQueueCreate(10, sizeof(int));
  ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(unit_hdl, &pcnt_cbs, queue));

  ESP_ERROR_CHECK(pcnt_unit_enable(unit_hdl));
  ESP_ERROR_CHECK(pcnt_unit_clear_count(unit_hdl));
  ESP_ERROR_CHECK(pcnt_unit_start(unit_hdl));

  ESP_LOGI(TAG, "Initiate and zero current sensor.");
  curr_sen.init();
  stop_motor();
  curr_sen.zero();

  ESP_LOGI(TAG, "Setting up update task.");
  xTaskCreatePinnedToCore(update_trampoline, "Update Task", update_config.stack_size, nullptr, update_config.priority, &update_task_hdl, update_config.core);

  ESP_LOGI(TAG, "Setting up formatting task.");
  xTaskCreatePinnedToCore(format_trampoline, "Format Task", format_config.stack_size, nullptr, format_config.priority, &format_task_hdl, format_config.core);

  ESP_LOGI(TAG, "Setting up PID controller task.");
  xTaskCreatePinnedToCore(pid_trampoline, "PID Controller Task", pid_config.stack_size, nullptr, pid_config.priority, &pid_task_hdl, pid_config.core);
  vTaskSuspend(pid_task_hdl);

  ESP_LOGI(TAG, "Setting up display task.");
  xTaskCreatePinnedToCore(display_task, "Display Task", display_config.stack_size, nullptr, display_config.priority, &display_task_hdl, display_config.core);
  vTaskSuspend(display_task_hdl);

  ESP_LOGI(TAG, "Initiate and set up communication task.");
  comm.init();
  xTaskCreatePinnedToCore(tx_data_task, "TX Data Task", tx_config.stack_size, nullptr, tx_config.priority, &tx_data_task_hdl, tx_config.core);

  // Allocate memory to vectors
  ESP_LOGI(TAG, "Allocating vector memory.");
  timestamp_vector[0].reserve(VECTOR_SIZE);
  direction_vector[0].reserve(VECTOR_SIZE);
  duty_cycle_vector[0].reserve(VECTOR_SIZE);
  velocity_vector[0].reserve(VECTOR_SIZE);
  position_vector[0].reserve(VECTOR_SIZE);
  current_vector[0].reserve(VECTOR_SIZE);

  timestamp_vector[1].reserve(VECTOR_SIZE);
  direction_vector[1].reserve(VECTOR_SIZE);
  duty_cycle_vector[1].reserve(VECTOR_SIZE);
  velocity_vector[1].reserve(VECTOR_SIZE);
  position_vector[1].reserve(VECTOR_SIZE);
  current_vector[1].reserve(VECTOR_SIZE);

  sample_vector.reserve(VECTOR_SIZE);
}

bool MotorController::pcnt_callback(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
  static uint64_t prev_time = 0;

  BaseType_t high_task_wakeup;
  QueueHandle_t queue = (QueueHandle_t)user_ctx;
  xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);

  motor_obj->sample_time = esp_timer_get_time();
  motor_obj->raw_velocity = CALI_FACTOR * ((double)SAMPLE_SIZE / (double)(motor_obj->sample_time - prev_time)) * PPUS_TO_RPM;

  prev_time = motor_obj->sample_time;
  return (high_task_wakeup == pdTRUE);
}

void MotorController::update_trampoline(void *arg)
{
  while (1)
  {
    motor_obj->update_task();
    vTaskDelay(update_config.delay / portTICK_PERIOD_MS);
  }
}

void MotorController::update_task()
{
  static int pcnt = 0;
  static MovingAverage velocity_average(VELOCITY_WINDOW_SIZE);

  ESP_ERROR_CHECK(pcnt_unit_get_count(unit_hdl, &pcnt));

  // Zero velocity if no counts for timeout interval
  if (esp_timer_get_time() - sample_time > TIMEOUT * US_TO_MS)
    raw_velocity = 0;

  auto now = chrono::system_clock::now();
  auto unix_time = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();

  timestamp = (uint64_t)unix_time;
  // velocity = velocity_average.next(raw_velocity);
  velocity = raw_velocity;
  abso_position = CALI_FACTOR * (double)pcnt * PULSE_TO_DEG;
  position = fmod(abso_position, 360.0); // Use calibration factor to adjust position to true value
  current = curr_sen.read_current();

  // Append values to vectors
  // timestamp_vector[curr_buffer].push_back(timestamp);
  // direction_vector[curr_buffer].push_back(direction);
  // duty_cycle_vector[curr_buffer].push_back(duty_cycle);
  // velocity_vector[curr_buffer].push_back(velocity);
  // position_vector[curr_buffer].push_back(position);
  // current_vector[curr_buffer].push_back(current);

  // if (timestamp_vector[curr_buffer].size() >= VECTOR_SIZE)
  // {
  //   curr_buffer = (curr_buffer + 1) % 2;
  //   buffer_ready = true;
  // }
}

void MotorController::format_trampoline(void *arg)
{
  while (1)
  {
    motor_obj->format_task();
    vTaskDelay(format_config.delay / portTICK_PERIOD_MS);
  }
}

void MotorController::format_task()
{
  if (buffer_ready)
  {
    format_samples();
    ESP_LOGI(TAG, "Formatted Sample: %s", sample_string.c_str());
    // for (auto sample : sample_vector)
    // {
    //   ESP_LOGI(TAG, "%s", sample.c_str());
    // }
    buffer_ready = false;
  }
}

void MotorController::pid_trampoline(void *arg)
{
  while (1)
  {
    motor_obj->pid_task();
    vTaskDelay(pid_config.delay / portTICK_PERIOD_MS);
  }
}

void MotorController::pid_task()
{
  static uint64_t prev_time = esp_timer_get_time();
  static uint64_t curr_time = 0;
  static double diff_time = 0;

  static double error_prev = 0;
  static double error = 0;
  static double integral = 0;
  static double derivative = 0;

  static double output_prev = 0;
  static double output = 0;

  curr_time = esp_timer_get_time();
  diff_time = (curr_time - prev_time) / US_TO_S;

  error = set_point - velocity;
  integral += error * diff_time;
  derivative = (error - error_prev) / diff_time;

  // Restrict integral to prevent integral windup
  if (integral > PID_WINDUP)
    integral = PID_WINDUP;
  if (integral < -PID_WINDUP)
    integral = -PID_WINDUP;

  output = kp * (error + 1 / ti * integral + td * derivative);

  // Restrict output to duty cycle range
  if (output > PID_MAX_OUTPUT)
    output = PID_MAX_OUTPUT;
  else if (output < PID_MIN_OUTPUT)
    output = PID_MIN_OUTPUT;

  // Controller only outputs a new value when error is outside oscillation threshold
  if (fabs(error) <= (set_point * PID_OSCILLATION))
    output = output_prev;

  set_duty_cycle(output);

  prev_time = curr_time;
  error_prev = error;
  output_prev = output;
}

void MotorController::display_task(void *arg)
{
  while (1)
  {
    ESP_LOGI(TAG, "Timestamp: %llu, Direction: %ld, Duty Cycle: %.5f, Velocity (RPM): %.5f, Position (Deg): %.5f, Current (mA): %.5f",
             motor_obj->timestamp,
             motor_obj->direction,
             motor_obj->duty_cycle,
             motor_obj->velocity,
             motor_obj->position,
             motor_obj->current);

    vTaskDelay(display_config.delay / portTICK_PERIOD_MS);
  }
}

void MotorController::tx_data_task(void *arg)
{
  static char data[128] = "";
  static char frame[256] = "";

  while (1)
  {
    sprintf(data, "%llu, %ld, %.5f, %.5f, %.5f, %.5f",
            motor_obj->timestamp,
            motor_obj->direction,
            motor_obj->duty_cycle,
            motor_obj->velocity,
            motor_obj->position,
            motor_obj->current);

    sprintf(frame, "%c, %s, %c\n", 0x1, data, 0x3);
    comm.send_data(frame);

    vTaskDelay(tx_config.delay / portTICK_PERIOD_MS);
  }
}

void MotorController::enable_display()
{
  ESP_LOGI(TAG, "Enabling display.");
  vTaskResume(display_task_hdl);
}

void MotorController::disable_display()
{
  ESP_LOGI(TAG, "Disabling display.");
  vTaskSuspend(display_task_hdl);
}

void MotorController::stop_motor()
{
  ESP_LOGI(TAG, "Stopping motor.");
  gpio_set_level(GPIO_IN1, 0);
  gpio_set_level(GPIO_IN2, 0);
}

void MotorController::set_mode(int32_t mode)
{
  this->mode = mode;

  switch (mode)
  {
  case MANUAL:
    ESP_LOGI(TAG, "Setting controller mode to manual.");
    vTaskSuspend(pid_task_hdl);
    break;
  case STOP:
    ESP_LOGI(TAG, "Stopping motor.");
    gpio_set_level(GPIO_IN1, 0);
    gpio_set_level(GPIO_IN2, 0);
    vTaskSuspend(pid_task_hdl);
    break;
  case AUTO:
    ESP_LOGI(TAG, "Setting controller mode to auto.");
    vTaskResume(pid_task_hdl);
    break;
  }
}

void MotorController::set_direction(int32_t direction)
{
  this->direction = direction;

  switch (direction)
  {
  case CLOCKWISE:
    ESP_LOGI(TAG, "Setting motor direction to clockwise.");
    gpio_set_level(GPIO_IN1, 1);
    gpio_set_level(GPIO_IN2, 0);
    break;
  case COUNTERCLOCKWISE:
    ESP_LOGI(TAG, "Setting motor direction to counter-clockwise.");
    gpio_set_level(GPIO_IN1, 0);
    gpio_set_level(GPIO_IN2, 1);
    break;
  }
}

void MotorController::set_duty_cycle(double duty_cycle)
{
  // if (duty_cycle > 1.0)
  //   duty_cycle = 1.0;

  this->duty_cycle = duty_cycle;

  if (mode == MANUAL)
    ESP_LOGI(TAG, "Setting motor duty cycle to %.5f.", duty_cycle);
  duty_cycle = (duty_cycle * MIN_DUTY_CYCLE) + MIN_DUTY_CYCLE; // Changes scale
  ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpr_hdl, TIMER_PERIOD * duty_cycle));
}

void MotorController::set_velocity(double set_point)
{
  this->set_point = set_point;

  ESP_LOGI(TAG, "Setting PID set point to %.5f.", set_point);
}

uint64_t MotorController::get_timestamp()
{
  return timestamp;
}

double MotorController::get_duty_cycle()
{
  return duty_cycle;
}

int32_t MotorController::get_direction()
{
  return direction;
}

double MotorController::get_velocity()
{
  return velocity;
}

double MotorController::get_position()
{
  return position;
}

double MotorController::get_current()
{
  return current;
}

void MotorController::format_samples()
{
  uint8_t prev_buffer = (curr_buffer + 1) % 2;
  stringstream sample;
  sample_string = "{";
  sample_vector.clear();

  for (uint16_t i = 0; i < VECTOR_SIZE; i++)
  {
    sample.str("");
    sample.precision(3);
    sample << "[" << fixed
           << timestamp_vector[prev_buffer][i] << ","
           << direction_vector[prev_buffer][i] << ","
           << duty_cycle_vector[prev_buffer][i] << ","
           << velocity_vector[prev_buffer][i] << ","
           << position_vector[prev_buffer][i] << ","
           << current_vector[prev_buffer][i] << "]";
    sample_vector.push_back(sample.str());
    sample_string += sample.str() + ",";
  }
  sample_string.pop_back();
  sample_string += "}";

  timestamp_vector[prev_buffer].clear();
  direction_vector[prev_buffer].clear();
  duty_cycle_vector[prev_buffer].clear();
  velocity_vector[prev_buffer].clear();
  position_vector[prev_buffer].clear();
  current_vector[prev_buffer].clear();
}