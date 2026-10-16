#include "MotorDebug.hpp"

#include "MotorControl.hpp"

struct MotorOpenLoopTest
{
    bool arm = false;
    uint8_t state = 0;
    int16_t command_current = 500;
    uint32_t duration_ticks = 300U;
    uint8_t packed_frame[8]{};
    uint32_t submitted_frames = 0;
    uint32_t failed_frames = 0;
    int32_t peak_abs_speed_rpm = 0;
    int16_t signed_speed_at_peak_rpm = 0;
    float signed_speed_at_peak_rad_s = 0.0f;
    uint8_t sample_count = 0;
    int16_t speed_samples_rpm[12]{};
    float position_samples_rad[12]{};
    float peak_abs_feedback_current = 0.0f;
    float start_position = 0.0f;
    float end_position = 0.0f;
};

struct MotorSpeedLoopTest
{
    bool arm = false;
    uint8_t state = 0;
    float target_speed_rad_s = 0.1f;
    uint32_t duration_ticks = 300U;
    uint16_t sample_count = 0;
    uint32_t sample_ticks[500]{};
    float feedback_rad_s[500]{};
    float error_rad_s[500]{};
    int16_t current_command[500]{};
    float current_feedback[500]{};
    uint32_t submitted_frames = 0;
    uint32_t failed_frames = 0;
};

struct MotorPositionLoopTest
{
    bool arm = false;
    uint8_t state = 0;
    float relative_target_rad = 0.2f;
    uint32_t duration_ticks = 5000U;
    float absolute_target_rad = 0.0f;
    uint16_t sample_count = 0;
    uint32_t sample_ticks[500]{};
    float position_rad[500]{};
    float position_error_rad[500]{};
    float speed_target_rad_s[500]{};
    float speed_feedback_rad_s[500]{};
    int16_t current_command[500]{};
    uint32_t submitted_frames = 0;
    uint32_t failed_frames = 0;
};

volatile MotorOpenLoopTest can1_id201_open_loop_test{};
volatile MotorSpeedLoopTest can1_id201_speed_loop_test{};
volatile MotorPositionLoopTest can1_id201_position_loop_test{};
volatile float can1_id201_test_speed_kp = 1800.0f;
volatile float can1_id201_test_speed_ki = 5.0f;
volatile float can1_id201_test_speed_kd = 0.0f;
volatile float can1_id201_test_speed_max_output = 500.0f;
volatile float can1_id201_test_speed_max_i_output = 150.0f;
volatile float can1_id201_test_position_kp = 3.5f;
volatile float can1_id201_test_position_ki = 0.002f;
volatile float can1_id201_test_position_kd = 0.0f;
volatile float can1_id201_test_position_max_output = 0.3f;
volatile float can1_id201_test_position_max_i_output = 0.05f;

namespace
{
constexpr float TEST_CURRENT_LIMIT = 500.0f;

uint32_t open_loop_start_tick = 0U;
uint32_t speed_loop_start_tick = 0U;
uint32_t position_loop_start_tick = 0U;
uint32_t current_control_tick = 0U;
bool open_loop_active_this_cycle = false;
bool speed_loop_active_this_cycle = false;
bool position_loop_active_this_cycle = false;

bool IsAllowedCurrent(int16_t current)
{
    int32_t magnitude = current;
    if (magnitude < 0)
        magnitude = -magnitude;
    return magnitude >= 100 && magnitude <= 500 && magnitude % 50 == 0;
}

bool IsAllowedSpeedTarget(float target)
{
    return target == 0.1f || target == -0.1f ||
           target == 0.5f || target == -0.5f ||
           target == 1.0f || target == -1.0f;
}

bool IsAllowedDuration(uint32_t duration_ticks)
{
    return duration_ticks == 300U ||
           duration_ticks == 1000U ||
           duration_ticks == 2000U ||
           duration_ticks == 5000U;
}

bool IsAllowedRelativePosition(float target)
{
    return target == 0.1f || target == -0.1f ||
           target == 0.2f || target == -0.2f ||
           target == 0.5f || target == -0.5f;
}

void UpdateOpenLoopTest(uint32_t now_ticks)
{
    const MotorControlSnapshot snapshot = MotorControl_GetSnapshot();

    if ((can1_id201_speed_loop_test.state == 1U ||
         can1_id201_position_loop_test.state == 1U) &&
        can1_id201_open_loop_test.arm)
    {
        can1_id201_open_loop_test.arm = false;
        can1_id201_open_loop_test.state = 3U;
    }

    if (can1_id201_open_loop_test.state == 0U && can1_id201_open_loop_test.arm)
    {
        can1_id201_open_loop_test.arm = false;
        if (snapshot.online &&
            can1_id201_position_loop_test.state != 1U &&
            IsAllowedCurrent(can1_id201_open_loop_test.command_current) &&
            IsAllowedDuration(can1_id201_open_loop_test.duration_ticks))
        {
            can1_id201_open_loop_test.sample_count = 0U;
            can1_id201_open_loop_test.submitted_frames = 0U;
            can1_id201_open_loop_test.failed_frames = 0U;
            can1_id201_open_loop_test.peak_abs_speed_rpm = 0;
            can1_id201_open_loop_test.signed_speed_at_peak_rpm = 0;
            can1_id201_open_loop_test.signed_speed_at_peak_rad_s = 0.0f;
            can1_id201_open_loop_test.peak_abs_feedback_current = 0.0f;
            can1_id201_open_loop_test.state = 1U;
            can1_id201_open_loop_test.start_position = snapshot.position_rad;
            open_loop_start_tick = now_ticks;
        }
        else
        {
            can1_id201_open_loop_test.state = 3U;
        }
    }

    if (can1_id201_open_loop_test.state != 1U)
        return;

    if (can1_id201_open_loop_test.sample_count < 12U)
    {
        const uint8_t index = can1_id201_open_loop_test.sample_count++;
        can1_id201_open_loop_test.speed_samples_rpm[index] = snapshot.speed_rpm;
        can1_id201_open_loop_test.position_samples_rad[index] = snapshot.position_rad;
    }

    int32_t speed = snapshot.speed_rpm;
    if (speed < 0)
        speed = -speed;
    if (speed > can1_id201_open_loop_test.peak_abs_speed_rpm)
    {
        can1_id201_open_loop_test.peak_abs_speed_rpm = speed;
        can1_id201_open_loop_test.signed_speed_at_peak_rpm = snapshot.speed_rpm;
        can1_id201_open_loop_test.signed_speed_at_peak_rad_s = snapshot.speed_rad_s;
    }

    float feedback_current = snapshot.current_feedback;
    if (feedback_current < 0.0f)
        feedback_current = -feedback_current;
    if (feedback_current > can1_id201_open_loop_test.peak_abs_feedback_current)
        can1_id201_open_loop_test.peak_abs_feedback_current = feedback_current;

    if (!snapshot.online ||
        now_ticks - open_loop_start_tick >= can1_id201_open_loop_test.duration_ticks)
    {
        MotorControl_Relax();
        can1_id201_open_loop_test.end_position = snapshot.position_rad;
        can1_id201_open_loop_test.state = snapshot.online ? 2U : 3U;
        return;
    }

    if (MotorControl_SetOpenLoopCurrent(can1_id201_open_loop_test.command_current))
        open_loop_active_this_cycle = true;
    else
        can1_id201_open_loop_test.state = 3U;
}

void UpdateSpeedLoopTest(uint32_t now_ticks)
{
    const MotorControlSnapshot snapshot = MotorControl_GetSnapshot();

    if (can1_id201_speed_loop_test.state == 0U && can1_id201_speed_loop_test.arm)
    {
        can1_id201_speed_loop_test.arm = false;
        const float target = can1_id201_speed_loop_test.target_speed_rad_s;
        const float kp = can1_id201_test_speed_kp;
        const float ki = can1_id201_test_speed_ki;
        const float kd = can1_id201_test_speed_kd;
        const float max_output = can1_id201_test_speed_max_output;
        const float max_i_output = can1_id201_test_speed_max_i_output;
        if (snapshot.online &&
            can1_id201_open_loop_test.state != 1U &&
            can1_id201_position_loop_test.state != 1U &&
            IsAllowedSpeedTarget(target) &&
            IsAllowedDuration(can1_id201_speed_loop_test.duration_ticks) &&
            kp >= 50.0f && kp <= 5000.0f &&
            ki >= 0.0f && ki <= 100.0f &&
            kd >= 0.0f && kd <= 2000.0f &&
            max_output >= 100.0f && max_output <= TEST_CURRENT_LIMIT &&
            max_i_output >= 0.0f && max_i_output <= max_output)
        {
            can1_id201_speed_loop_test.sample_count = 0U;
            can1_id201_speed_loop_test.submitted_frames = 0U;
            can1_id201_speed_loop_test.failed_frames = 0U;
            const MotorSpeedPidConfig config{kp, ki, kd, max_output, max_i_output};
            MotorControl_ConfigureSpeedPid(config);
            if (MotorControl_SetSpeedTarget(target))
            {
                speed_loop_start_tick = now_ticks;
                can1_id201_speed_loop_test.state = 1U;
            }
            else
            {
                can1_id201_speed_loop_test.state = 3U;
            }
        }
        else
        {
            can1_id201_speed_loop_test.state = 3U;
        }
    }

    if (can1_id201_speed_loop_test.state != 1U)
        return;

    if (!snapshot.online ||
        now_ticks - speed_loop_start_tick >= can1_id201_speed_loop_test.duration_ticks)
    {
        MotorControl_Relax();
        can1_id201_speed_loop_test.state = snapshot.online ? 2U : 3U;
        return;
    }

    speed_loop_active_this_cycle = true;
}

void UpdatePositionLoopTest(uint32_t now_ticks)
{
    const MotorControlSnapshot snapshot = MotorControl_GetSnapshot();

    if (can1_id201_position_loop_test.state == 0U && can1_id201_position_loop_test.arm)
    {
        can1_id201_position_loop_test.arm = false;
        const float relative_target = can1_id201_position_loop_test.relative_target_rad;
        const float kp = can1_id201_test_position_kp;
        const float ki = can1_id201_test_position_ki;
        const float kd = can1_id201_test_position_kd;
        const float max_output = can1_id201_test_position_max_output;
        const float max_i_output = can1_id201_test_position_max_i_output;
        if (snapshot.online &&
            can1_id201_open_loop_test.state != 1U &&
            can1_id201_speed_loop_test.state != 1U &&
            IsAllowedRelativePosition(relative_target) &&
            IsAllowedDuration(can1_id201_position_loop_test.duration_ticks) &&
            kp >= 0.1f && kp <= 20.0f &&
            ki >= 0.0f && ki <= 5.0f &&
            kd >= 0.0f && kd <= 10.0f &&
            max_output >= 0.1f && max_output <= 0.5f &&
            max_i_output >= 0.0f && max_i_output <= max_output)
        {
            can1_id201_position_loop_test.sample_count = 0U;
            can1_id201_position_loop_test.submitted_frames = 0U;
            can1_id201_position_loop_test.failed_frames = 0U;
            const MotorPositionPidConfig config{kp, ki, kd, max_output, max_i_output};
            MotorControl_ConfigurePositionPid(config);
            can1_id201_position_loop_test.absolute_target_rad = snapshot.position_rad + relative_target;
            if (MotorControl_SetPositionTarget(can1_id201_position_loop_test.absolute_target_rad))
            {
                position_loop_start_tick = now_ticks;
                can1_id201_position_loop_test.state = 1U;
            }
            else
            {
                can1_id201_position_loop_test.state = 3U;
            }
        }
        else
        {
            can1_id201_position_loop_test.state = 3U;
        }
    }

    if (can1_id201_position_loop_test.state != 1U)
        return;

    if (!snapshot.online ||
        now_ticks - position_loop_start_tick >= can1_id201_position_loop_test.duration_ticks)
    {
        MotorControl_Relax();
        can1_id201_position_loop_test.state = snapshot.online ? 2U : 3U;
        return;
    }

    position_loop_active_this_cycle = true;
}
}

void MotorDebug_BeforeControl(uint32_t now_ticks)
{
    current_control_tick = now_ticks;
    open_loop_active_this_cycle = false;
    speed_loop_active_this_cycle = false;
    position_loop_active_this_cycle = false;
    UpdateOpenLoopTest(now_ticks);
    UpdateSpeedLoopTest(now_ticks);
    UpdatePositionLoopTest(now_ticks);
}

void MotorDebug_AfterControl(HAL_StatusTypeDef send_status)
{
    const MotorControlSnapshot snapshot = MotorControl_GetSnapshot();

    if (open_loop_active_this_cycle)
    {
        for (uint8_t i = 0U; i < 8U; ++i)
            can1_id201_open_loop_test.packed_frame[i] = snapshot.can1_control_frame[i];

        if (send_status == HAL_OK)
        {
            ++can1_id201_open_loop_test.submitted_frames;
        }
        else
        {
            ++can1_id201_open_loop_test.failed_frames;
            MotorControl_Relax();
            can1_id201_open_loop_test.state = 3U;
        }
    }

    if (speed_loop_active_this_cycle)
    {
        if (can1_id201_speed_loop_test.sample_count < 500U)
        {
            const uint16_t index = can1_id201_speed_loop_test.sample_count++;
            can1_id201_speed_loop_test.sample_ticks[index] =
                current_control_tick - speed_loop_start_tick;
            can1_id201_speed_loop_test.feedback_rad_s[index] = snapshot.speed_rad_s;
            can1_id201_speed_loop_test.error_rad_s[index] = snapshot.speed_error_rad_s;
            can1_id201_speed_loop_test.current_command[index] = snapshot.current_command;
            can1_id201_speed_loop_test.current_feedback[index] = snapshot.current_feedback;
        }

        if (send_status == HAL_OK)
        {
            ++can1_id201_speed_loop_test.submitted_frames;
        }
        else
        {
            ++can1_id201_speed_loop_test.failed_frames;
            MotorControl_Relax();
            can1_id201_speed_loop_test.state = 3U;
        }
    }

    if (position_loop_active_this_cycle)
    {
        if (can1_id201_position_loop_test.sample_count < 500U)
        {
            const uint16_t index = can1_id201_position_loop_test.sample_count++;
            can1_id201_position_loop_test.sample_ticks[index] =
                current_control_tick - position_loop_start_tick;
            can1_id201_position_loop_test.position_rad[index] = snapshot.position_rad;
            can1_id201_position_loop_test.position_error_rad[index] = snapshot.position_error_rad;
            can1_id201_position_loop_test.speed_target_rad_s[index] = snapshot.position_speed_target_rad_s;
            can1_id201_position_loop_test.speed_feedback_rad_s[index] = snapshot.speed_rad_s;
            can1_id201_position_loop_test.current_command[index] = snapshot.current_command;
        }

        if (send_status == HAL_OK)
        {
            ++can1_id201_position_loop_test.submitted_frames;
        }
        else
        {
            ++can1_id201_position_loop_test.failed_frames;
            MotorControl_Relax();
            can1_id201_position_loop_test.state = 3U;
        }
    }
}
