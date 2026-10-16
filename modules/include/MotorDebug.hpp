#ifndef MOTORDEBUG_HPP
#define MOTORDEBUG_HPP

#include "main.h"

#include <cstdint>

void MotorDebug_BeforeControl(uint32_t now_ticks);
void MotorDebug_AfterControl(HAL_StatusTypeDef send_status);

#endif // MOTORDEBUG_HPP
