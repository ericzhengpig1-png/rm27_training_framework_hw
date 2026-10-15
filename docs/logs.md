# 调试与开发日志

## 2026-10-07：I2C3 内部上拉配置

开发：为 I2C3 的 PA8/SCL 与 PC9/SDA 配置 `GPIO_PULLUP`，并同步更新 CubeMX 配置文件 `board/board.ioc` 与生成代码 `board/Core/Src/i2c.c`。两根引脚仍为开漏复用输出（`GPIO_MODE_AF_OD`），复用功能仍为 `GPIO_AF4_I2C3`。

验证目的：确认这次板级 GPIO 配置修改没有破坏 STM32F407 的交叉编译与固件链接。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=board/cmake/gcc-arm-none-eabi.cmake
cmake --build <临时目录>
```

终端结果：

```text
[230/230] Linking C executable board.elf
RAM:   36936 B / 128 KiB  (28.18%)
FLASH: 25388 B / 1 MiB   (2.42%)
```

结论：构建通过，I2C3 引脚配置已参与 `i2c.c` 编译并成功链接至 `board.elf`。尚未在真实 C 板上验证 I2C 从设备通信。

## 2026-10-07：BSP CAN 最小安全发送

开发：实现 `CAN_Transmit()` 的 CAN1/CAN2 就绪检查、11 位标准 ID 检查、0–8 字节长度检查、空数据检查、邮箱空闲检查与标准数据帧发送；`CAN_Init()` 仅在过滤器配置、控制器启动和 RX FIFO0 通知均成功后标记对应 CAN 总线可发送。将 `bsp/src/bsp_can.cpp` 与 `bsp/include` 接入 `board` 目标，并明确 C++11 编译标准。

验证目的：确认 CAN BSP 源文件可由 ARM C++ 编译器编译并链接到固件，且不会因 C/C++ 混合链接失败。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=board/cmake/gcc-arm-none-eabi.cmake
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../bsp_can.cpp.obj
Linking CXX executable board.elf
RAM:   36936 B / 128 KiB  (28.18%)
FLASH: 25388 B / 1 MiB   (2.42%)
```

结论：构建通过，`bsp_can.cpp` 已由 `arm-none-eabi-g++` 编译并成功链接。尚未在真实 CAN 总线验证报文 ID、数据字节序与接收端应答。

## 2026-10-07：真机验证计划

开发：新增 `docs/hardware_validation.md`，集中维护软件功能完成后的真机操作和预期现象；当前包含 I2C3 与 CAN，其他外设将在对应软件功能完成后补充。

结论：软件开发阶段不执行真机验证，当前没有新增构建或硬件测试结果。

## 2026-10-07：BSP CAN RX FIFO0 回调

开发：新增 `CAN_RegisterRxCallback()`，用于注册一个标准数据帧处理函数；实现 HAL 的 `HAL_CAN_RxFifo0MsgPendingCallback()`，从 RX FIFO0 读取一帧 CAN 数据，过滤扩展帧、远程帧和异常 DLC 后，交给已注册的回调。BSP 不包含或调用电机模块，电机 ID 的识别和反馈解析保留给后续 modules 层。

验证目的：确认 CAN 接收回调能以 C 链接名被 HAL 调用，并与 C++ BSP 源文件共同编译、链接到固件。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=board/cmake/gcc-arm-none-eabi.cmake
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../bsp_can.cpp.obj
Linking CXX executable board.elf
RAM:   36944 B / 128 KiB  (28.19%)
FLASH: 26164 B / 1 MiB   (2.50%)
```

结论：构建通过，RX FIFO0 回调已链接到 `board.elf`。尚未在真实 CAN 总线验证中断触发、报文内容和回调次数。

## 2026-10-07：BSP UART 安全发送

开发：实现 `USART_Transmit()` 的 USART1/USART6 句柄检查、初始化状态检查、空数据和零长度检查，并按阻塞、DMA、中断三种模式调用对应 HAL 发送函数。将 `bsp/src/bsp_usart.cpp` 接入 `board` 目标；不修改 CubeMX 生成的 UART、DMA 和中断配置，不实现接收缓冲区或空闲中断。

验证目的：确认 UART BSP 源文件可由 ARM C++ 编译器编译并链接到固件，且三种 HAL 发送接口的调用路径均可编译。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=board/cmake/gcc-arm-none-eabi.cmake
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../bsp_usart.cpp.obj
Linking CXX executable board.elf
RAM:   36944 B / 128 KiB  (28.19%)
FLASH: 26164 B / 1 MiB   (2.50%)
```

结论：构建通过，`bsp_usart.cpp` 已由 `arm-none-eabi-g++` 编译并成功链接。尚未在真实串口验证三种发送模式的字节输出。

## 2026-10-07：BSP UART 通用 DMA 接收接口

开发：将 `USART_Receive()` 改为同时支持 USART1、USART6 的 DMA 接收至空闲事件，并返回 HAL 状态；新增每路 UART 一个接收回调和调用方上下文。HAL 的接收事件回调只转发 UART 句柄、调用方缓冲区、DMA 写入位置和事件类型，不解析串口助手、裁判系统或遥控器协议。调用模块负责提供持续有效的缓冲区，并在回调中处理数据。

验证目的：确认 C++ BSP 能正确覆盖 HAL 的接收事件回调，并能链接 `HAL_UARTEx_ReceiveToIdle_DMA()` 与 `HAL_UARTEx_GetRxEventType()`。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../bsp_usart.cpp.obj
[232/232] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

调试记录：首次构建时 PowerShell 将未加引号的工具链文件参数错误拆分，导致 CMake 找不到 `.cmake` 后缀；改用加引号的绝对工具链路径后，配置和构建均通过。这是命令传参问题，不是源码或工具链错误。

结论：通用接收接口已通过交叉编译。尚未在真机验证 DMA 事件、两路状态隔离或设备协议解析；相应操作和预期现象已加入 `docs/hardware_validation.md`。

## 2026-10-07：BSP UART 接收注释补充

开发：为 UART 通用接收新增的状态结构、状态查询、回调注册、DMA 接收启动和 HAL 接收事件回调补充输入、输出和关键步骤的简明中文注释。

验证目的：提升接口学习与后续模块接入时的可读性；本次只修改注释，不改变编译产物或运行逻辑。

检查操作：

```text
git diff --check
```

终端结果：检查通过，未发现空白格式错误。Git 的 LF/CRLF 提示仅表示 Windows 行尾转换，不是代码错误。

结论：注释修改已完成且格式正确；由于没有改变运行逻辑，无需重复进行行为构建验证。

## 2026-10-08：BSP PWM 启停接口

开发：将 `PWM_Start()`、`PWM_Stop()` 的返回类型由 `void` 改为 `HAL_StatusTypeDef`。函数仅接受当前板级已配置的 `TIM8/TIM10` 的 `TIM_CHANNEL_1`；空指针、其他定时器或其他通道直接返回 `HAL_ERROR`，其余情况调用并返回 HAL 的 PWM 启动/停止结果。将 `bsp/src/bsp_pwm.cpp` 加入 `board` 构建目标。本次不修改 CubeMX 的 TIM8/TIM10 配置，不启动 PWM，不修改周期或占空比。

验证目的：确认 PWM BSP 能参与 ARM 交叉编译，且启动/停止接口能把 HAL 的成功或失败状态返回给上层调用者。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../bsp_pwm.cpp.obj
[233/233] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：构建通过，PWM 启停接口已链接到固件。尚未在真机验证 PC6（TIM8_CH1）与 PF6（TIM10_CH1）的实际 PWM 输出；后续统一按 `docs/hardware_validation.md` 进行硬件验证。

## 2026-10-08：BSP PWM 周期与占空比

开发：实现 `PWM_SetPeriod()` 和 `PWM_SetDutyRatio()`，并将两者返回类型改为 `HAL_StatusTypeDef`。周期设置仅支持已完成 CubeMX 初始化的 TIM8/TIM10：读取 APB2 定时器时钟和当前 PSC，换算输入秒数为 16 位 ARR；同步按原比例换算 CCR1，从而保持现有占空比。占空比设置仅支持 TIM8/TIM10 的 CH1，将 0~1 比例换算为 CCR1。空指针、未初始化定时器、未知通道、非正或超出 16 位范围的周期，以及不在 0~1 范围内的占空比均返回 `HAL_ERROR`。补充了 PC6/PF6 的统一真机验证操作和预期现象。

验证目的：确认 PWM 周期与占空比寄存器计算可由 ARM C++ 编译器编译、链接；确认非法输入在软件接口层被拒绝。真机波形与负载行为留待统一硬件验证阶段。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

调试记录：首次构建报错，当前 STM32 HAL 未提供 `__HAL_TIM_GET_PRESCALER()` 宏。将预分频值读取改为 `htim->Instance->PSC` 后重新构建通过；这是 HAL 宏可用性差异，不是定时器配置错误。

终端结果：

```text
Building CXX object .../bsp_pwm.cpp.obj
[233/233] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：构建通过，PWM 周期与占空比接口已链接到固件。尚未在真机验证波形、PWM 停止后的引脚状态或加热负载；相应步骤已写入 `docs/hardware_validation.md`。

## 2026-10-08：BSP CAN / UART 发送状态返回

开发：将 `CAN_Transmit()` 和 `USART_Transmit()` 的返回类型由 `void` 改为 `HAL_StatusTypeDef`。CAN 发送在总线未准备、句柄/ID/DLC/数据参数无效时返回 `HAL_ERROR`，发送邮箱无空闲位置时返回 `HAL_BUSY`，其他情况直接返回 `HAL_CAN_AddTxMessage()` 的状态。UART 发送在句柄、数据、长度、状态或模式无效时返回 `HAL_ERROR`，其余情况直接返回阻塞、DMA 或中断 HAL 发送函数的状态。补充真机验证文档：CAN 的 `HAL_OK` 表示报文已进入发送邮箱，DMA/中断 UART 的 `HAL_OK` 表示发送已启动而非已经完成。

验证目的：确认 CAN/UART 的发送结果能被上层模块获取，且公开接口返回值变更能通过 ARM C++ 编译、链接。未进行真机发送测试。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

终端结果：

```text
[233/233] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：构建通过，CAN/UART 发送接口已能向上层报告成功、忙、超时或错误状态。尚未验证真实 CAN 邮箱满、CAN 总线应答、UART DMA/中断完成时序；相应真机操作与预期现象已写入 `docs/hardware_validation.md`。

## 2026-10-08：libs PID 位置式与增量式计算

开发：补全 `PID::UpdateResult()`。位置式 PID 计算当前的 P、I、D 三项，积分项限制在 `maxIOut`，三项之和限制在 `maxOut`。增量式 PID 计算 P、D 的本次变化量，并保存受限的积分状态，只将积分项的本次变化量加入已有输出；最终输出同样限制在 `maxOut`。未知 PID 模式保持零输出，避免生成未知控制量。将 `crc.cpp`、`math.cpp`、`pid.cpp` 及 `libs/include` 加入板级 CMake 工程，使库代码能参与固件构建。

验证目的：确认 PID 库能被 ARM 交叉编译器实际编译、链接；用可手算的数据验证两种 PID 模式、积分限幅、最终输出限幅以及未知模式的安全输出。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../libs/src/crc.cpp.obj
Building CXX object .../libs/src/math.cpp.obj
Building CXX object .../libs/src/pid.cpp.obj
[236/236] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

数值测试：位置式与增量式 PID 均使用 `kp=1`、`ki=0.5`、`kd=0.2`、`maxOut=10`、`maxIOut=3`，输入误差为 4。第一次计算的输出为 6.8，第二次积分到达 3 后输出为 7；位置式 PID 额外使用 `maxOut=5`、`maxIOut=2` 验证积分与总输出限幅；未知模式验证 P、I、D 和最终输出均为 0。

调试记录：首次在 Windows 上运行临时数值测试程序时，程序依赖的 MinGW 动态运行库未找到，退出码为 `-1073741511`。改为静态链接的临时测试程序后，终端返回：

```text
PID numeric checks passed
PID_TEST_EXIT_CODE=0
```

结论：PID 的两种计算方式和限幅逻辑已通过主机数值测试，并已通过 ARM 固件交叉编译。尚未把 PID 接入 M2006 或 BMI088 温控，PID 参数也尚未进行真机整定。

## 2026-10-08：libs CRC8 空指针保护

开发：为 `Get_CRC8_Check_Sum()` 补充空指针检查。调用者传入空数据指针时，函数不再读取无效内存，而是直接返回传入的初始 CRC 值；正常 CRC8 计算路径不变。

验证目的：确认 CRC8 公开计算接口在异常输入下不会发生空指针访问，同时保持正常报文的 CRC8 附加与校验行为。

数值测试：传入 `nullptr`、长度 8、初始 CRC 值 `0x5A`，预期并实际返回 `0x5A`；对 `{0x01, 0x02, 0x03}` 追加 CRC8 后，校验通过；篡改其中一个数据字节后，校验失败。

终端结果：

```text
CRC8 checks passed
CRC8_TEST_EXIT_CODE=0
```

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

终端结果：

```text
[236/236] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：CRC8 对空指针输入已安全返回，正常附加与校验流程通过主机数值测试，并通过 ARM 固件交叉编译。

## 2026-10-08：SPI 与 BMI088 学习笔记

开发：整理 SPI 与 BMI088 通信的学习笔记，新增 `docs/spi.md`，并将四张讲解图片保存到 `assets/spi/`。笔记按 SPI 四线含义、BMI088 实际连接、Chip ID 读取时序、模块上下游关系和完整生命周期组织；明确记录 `ReadReg()` 采用方案 B，即在模块内部处理加速度计 SPI 读取中的无效占位字节，对外只返回有效寄存器数据。

验证目的：确保学习笔记的图片使用仓库内稳定相对路径，避免引用系统临时目录；确认文档中的项目引脚、Chip ID 和模块职责与当前仓库配置一致。

检查操作：确认 `assets/spi/` 中的四张图片存在，并检查 Markdown 相对引用和 Git 差异格式。

结论：SPI / BMI088 的学习材料已沉淀到仓库文档。此次仅更新文档和图片资源，不修改源代码，因此无需执行 CMake 构建。

## 2026-10-08：BMI088 第一阶段——SPI 寄存器通信与 Chip ID 校验

开发：将 `modules/src/BMI088.cpp` 和 `modules/include` 加入 `board` 的 CMake 构建目标。实现 BMI088 共用 SPI1 的片选映射：PA4/ACC_CS 选择加速度计，PB0/GYRO_CS 选择陀螺仪；`WriteReg()` 按写地址和数据的顺序传输，`ReadReg()` 按读地址和数据的顺序传输。加速度计读取时，模块内部额外时钟并丢弃首个无效字节，因此调用者只得到请求的有效寄存器数据；陀螺仪读取不执行该步骤。两个函数均返回 HAL 通信状态。`VerifyAccChipID()` 与 `VerifyGyroChipID()` 现在分别检查通信状态和 `0x1E` / `0x0F`，再共同更新 `INIT_ERR`。

验证目的：确认 BMI088 模块已经使用当前板级 SPI1、PA4 与 PB0 定义，且寄存器读写、两颗芯片的 ID 校验可以参与 STM32F407 固件的 C++ 编译与链接。软件阶段不连接或运行真实 BMI088。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../modules/src/BMI088.cpp.obj
[237/237] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

调试记录：本次首次构建即通过，无新增编译或链接错误。代码审查确认加速度计无效字节只在 `ReadReg()` 内部处理，避免上层 Chip ID、原始数据与温度读取重复丢弃数据。

结论：BMI088 的 SPI 寄存器传输与双 Chip ID 软件校验已完成并通过 ARM 交叉编译。`ReadAccData()`、`ReadGyroData()`、`ReadAccTemperature()`、数据有效性校验、标定和温控仍未实现；真实 Chip ID、SPI 时序和接线验证尚未执行，具体操作与预期现象已加入 `docs/hardware_validation.md`。

## 2026-10-08：BMI088 批次 A——配置审查与物理数据读取

开发：根据 [Bosch BMI088 数据手册](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi088-ds001.pdf) 审查 `Config()`。加速度计保持原有的 ±6 g、正常滤波和 800 Hz 输出数据率：将原来的魔法数 `0xAB` 改为 `ACC_CONF_BWP_NORM | ACC_CONF_ODR_800_Hz`，并把带宽常量修正为寄存器实际使用的 [7:4] 位值。陀螺仪保持 ±2000 °/s、2000 Hz / 230 Hz 带宽，但删除了错误混入 `GYRO_BANDWIDTH` 的电源模式位。实现 `ReadAccData()`、`ReadGyroData()` 和 `ReadAccTemperature()`：三轴数据从 LSB 起连续读取 6 字节，按补码组合为 `int16_t`，再分别换算为 `m/s²` 与 `rad/s`；温度按 11 位补码转换为 `°C`。加速度数据结构同时写入温度。陀螺仪零偏数组初始化为零，确保标定前的输出确定。

验证目的：确认 BMI088 的量程配置与使用的换算系数相符；确认 16 位补码、11 位温度补码及三个读取函数均能通过 STM32F407 ARM 交叉编译。真机数据、安装方向和静止零偏留待统一硬件验证。

公式检查：使用字节样例 `{0x34, 0x12}` 和 `{0xFC, 0xFF}` 验证 16 位补码解析分别为 `4660` 和 `-4`；使用数据手册温度样例验证 `{0x00, 0x60}` 为 `23.375 °C`，`{0xC1, 0x00}` 为 `-40 °C`。终端结果：

```text
BMI088_FORMULA_CHECKS_PASSED
SIGNED_0x1234=4660
SIGNED_0xFFFC=-4
TEMP_0x0060=23.375
TEMP_0xC100=-40
```

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../modules/src/BMI088.cpp.obj
[237/237] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

调试记录：首次公式检查的 PowerShell 辅助函数未显式处理 16 位补码的负值，且不代表固件代码错误；将辅助检查改为按 16 位补码规则显式减去 `0x10000` 后，正负样例均通过。ARM 固件构建首次通过，没有新增编译或链接错误。

结论：BMI088 现已能在软件接口层输出加速度 `m/s²`、角速度 `rad/s` 和温度 `°C`。本批次未实现数据有效性判断、标定、温控或线程调用；真实数据和 SPI 时序尚未验证，相关操作与预期现象已加入 `docs/hardware_validation.md`。

## 2026-10-09：BMI088 批次 B——数据有效性与陀螺仪零偏标定

开发：为 `cBMI088` 增加加速度和陀螺仪的最近一次 SPI 读取结果。`ReadAccData()`、`ReadGyroData()` 在读成功时同步更新对象保存的 `acc_data`、`gyro_data`，在失败时清零调用方输出及对象保存的数据。`VerifyAccData()`、`VerifyGyroData()` 现在检查最近一次读取是否成功、各轴是否为有限浮点数，以及是否超过当前 ±6 g、±2000 °/s 配置换算出的输出边界，再分别更新 `ACC_DATA_ERR` 和 `GYRO_DATA_ERR`。

`Calibrate()` 在采样前保存旧零偏并临时将零偏置零；4000 次读取过程中任意一次陀螺仪数据校验失败，会立即恢复旧零偏并设置 `CALIBRATE_ERR`。成功采样后使用三轴平均值作为新零偏；任意轴平均零偏超过既有的 `0.1 rad/s` 上限时，同样恢复旧零偏并标记标定失败。这样不会因一次通信失败而将“全零假数据”误写成有效零偏。

验证目的：确认新增的读取状态、有限值和范围检查，以及标定中的失败回滚逻辑可以由 ARM C++ 编译器编译并链接。由于本阶段不连接 BMI088，SPI 失败、静止零偏和运动标定的实际行为留待统一真机验证。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../modules/src/BMI088.cpp.obj
[237/237] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：数据有效性和标定失败回滚逻辑已通过 ARM 交叉编译。当前数据有效性只判断通信结果、浮点数有效性和传感器量程边界；振动、方差和“是否静止”的具体阈值不在没有真机数据的情况下猜测。真机操作、预期现象和当前限制已补充到 `docs/hardware_validation.md`。
## 2026-10-09：BMI088 批次 C——温度闭环控制（软件实现）

开发：完成 `TemperatureControl()` 的最小闭环。函数先读取 BMI088 加速度计内置温度，再以目标温度为 PID 参考值、当前温度为反馈值计算加热占空比，最后通过 TIM10/PF6 的 PWM 接口输出。原 `TempPid` 最大输出为 `25000`，而 `PWM_SetDutyRatio()` 的输入单位是 `0.0f ~ 1.0f` 的占空比；现将温控 PID 输出范围统一为 `0.0f ~ 1.0f`，避免将不同单位直接混用。目标温度低于或等于当前温度时，负的 PID 输出会限制为 `0.0f`，不再加热。

保护逻辑：温度读取失败、目标温度不是有限数，或 PWM 设置/启动失败时，函数会清除 PID 状态、请求将占空比归零，并将 `TEMP_CTRL_ERR` 置为 `true`。增加 `TemperatureReadOk`，避免通信失败时把默认的 `0.0 °C` 当作真实温度用于加热计算。当前 `kp=0.1`、`ki=0`、`kd=0` 仅保留已有的最小软件参数，不表示已经完成真机热系统整定。

验证目的：确认温度读取状态、PID 输出单位和 PWM 接口能够由 ARM C++ 工程正确编译、链接。真机加热方向、升温速度、过冲与 PID 参数均留待统一硬件验证。

构建操作：

```text
cmake -S board -B <临时目录> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../modules/src/BMI088.cpp.obj
[237/237] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：`TemperatureControl()` 已完成软件闭环并通过 ARM 交叉编译。该函数尚未被业务线程调用，且没有执行任何真机加热操作；加热方向、PWM 波形和 PID 热系统参数需严格按 `docs/hardware_validation.md` 在后续统一验证。

## 2026-10-09：BMI088 全链路静态检查

检查范围：核对 CubeMX 板级初始化、CMake 编译接入、BMI088 模块接口实现、ThreadX 调用入口和温控 PWM 调用顺序；本次未修改任何源代码或板级配置，未进行真机测试。

检查结果：`main.c` 已依次初始化 GPIO、SPI1、TIM10 后进入 ThreadX；SPI1 的 Mode 3、软件片选及 PA4/ACC_CS、PB0/GYRO_CS 与 BMI088 模块定义一致，TIM10_CH1/PF6 已配置为加热 PWM。`BMI088.cpp` 已被 CMake 编译并成功链接，但全仓库尚无 `cBMI088` 对象创建，也无对 `Config()`、Chip ID 校验、数据读取、标定或 `TemperatureControl()` 的外部调用。`App_ThreadX_Init()` 当前没有创建业务线程，因此完整链路尚不能在固件中实际运行。

风险记录：`TemperatureControl()` 当前每次调用都会执行 `PWM_Start()`；STM32 HAL 在 PWM 通道已经处于 BUSY 状态时会返回 `HAL_ERROR`，因此该函数不能按温控需求被周期性重复调用。`Config()` 也没有处理各次 `WriteReg()` 的返回值，SPI 配置写入失败不会直接反映为配置错误。BMI088 配置中启用了数据就绪中断映射，但当前板级工程没有相应的 BMI088 中断引脚、EXTI 配置或回调；轮询读取不依赖它，但该配置目前没有实际使用者。

构建操作：

```text
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../modules/src/BMI088.cpp.obj
[2/2] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

格式检查：`git diff --check` 发现 `modules/src/BMI088.cpp` 第 56、57 行存在两处注释行尾空格；不影响构建或运行，本次按只读检查原则未修改。

结论：BMI088 的“板级初始化到模块代码”的编译链已连通，但“模块代码到实际运行”的线程入口尚未建立；在创建 IMU 线程前，应先修正 PWM 重复启动和配置写入失败不可见这两项逻辑问题。

## 2026-10-09：BMI088 批次 D——温控周期调用与配置失败保护

开发：在 `cBMI088` 内增加 `HeatingPwmStarted` 与 `ConfigOk` 两个对象私有状态，不引入线程、全局状态或新的 BSP 接口。`TemperatureControl()` 成功设置首次占空比后，仅在 PWM 尚未启动时调用一次 `PWM_Start()`；之后的周期调用只更新占空比，避免已处于 BUSY 状态的 TIM10_CH1 被重复启动。温度读取失败、占空比设置失败或首次 PWM 启动失败时，均清除 `TempPid`、请求将占空比归零并设置 `TEMP_CTRL_ERR`。

`Config()` 现在通过局部写入检查函数处理每一次 `WriteReg()`：任一次 SPI 写入失败都会使 `ConfigOk` 变为 `false`、设置 `INIT_ERR` 并立即停止后续配置。Chip ID 校验汇总初始化状态时会同时检查 `ConfigOk`，因此后续的 Chip ID 成功不会覆盖之前的配置写入失败。

验证目的：确认 PWM 重复启动路径已由对象状态隔离，所有 BMI088 配置写入均经过返回值检查，并确认新增 C++ 状态和局部 lambda 可被 ARM 工具链编译。未创建 IMU 线程，未执行真机 SPI 或加热测试。

构建操作：

```text
cmake --build <临时目录>
```

终端结果：

```text
Building CXX object .../modules/src/BMI088.cpp.obj
[2/2] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：两个已发现的局部逻辑问题已在 BMI088 模块内修正并通过 ARM 交叉编译。IMU 线程入口、BMI088 数据就绪中断链路和真机验证仍不在本批次范围内。

## 2026-10-10：M2006 批次 A——协议参数、注册与反馈解析（软件实现）

开发：按 M2006 配合 C610 的常规 CAN 协议，在 `modules/include/DJIMotorHandler.hpp` 集中定义反馈 ID `0x201~0x208`、两组控制 ID `0x200` / `0x1FF` 与编码器分辨率 `8192`；在 `modules/include/M2006.hpp` 集中定义减速比 `36:1` 和最大电流指令 `±10000`。后续若改用其他电机、电调或协议，应先从这两处重新确认参数。

`registerMotor()` 现只接受 CAN1/CAN2 与 `0x201~0x208`，将电机登记到“总线 + ID”对应槽位，并拒绝不同对象覆盖已登记槽位。`updateFeedback()` 按总线和 ID 索引找到已登记电机；`UpdateSensorData()` 解析 8 字节反馈帧中的编码器、转速、电流和温度。首次反馈将输出轴位置定义为 `0 rad`；之后处理编码器从 `8191` 回到 `0` 或反向回绕，并按 `36:1` 换算输出轴角速度（rad/s）和连续位置（rad）。本批只支持 `GearBox_M2006` 的物理量换算。

安全边界：`M2006` 构造函数只设置电机类型和最大允许电流，基类保持松弛模式、`currentSet = 0`。本批未实现控制报文发送、PID、`setOutput()`、在线判定、CAN 回调注册或线程入口，因此不会新增任何实际电机电流输出。

调试记录：首次配置命令因 PowerShell 将未引用的 `.cmake` 后缀拆分为额外参数，CMake 未能加载工具链文件，尚未进入源代码编译；改为使用带引号的工具链绝对路径后，配置与构建正常完成。

验证目的：确认新增协议常量、M2006 物理量换算、CAN 登记/反馈接口以及 CMake 源文件接入可被 ARM C++ 工具链完整编译和链接。未连接 CAN 总线、电调或电机，反馈字节解析、位置方向与电机实际 ID 均留待统一真机验证。

构建操作：

```text
cmake -S board -B build/codex-m2006-batch-a -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_TOOLCHAIN_FILE=<board/cmake/gcc-arm-none-eabi.cmake 的绝对路径>"
cmake --build build/codex-m2006-batch-a
```

终端结果：

```text
Building CXX object .../modules/src/DJIMotorHandler.cpp.obj
Building CXX object .../modules/src/M2006.cpp.obj
[239/239] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：M2006 的协议基础层已通过 ARM 交叉编译。下一批才实现控制电流报文打包和发送；PID 与线程调用仍不在本批范围内。

## 2026-10-10：M2006 批次 B——控制报文打包与发送（软件实现）

开发：完成 `DJIMotorHandler::sendControlData()`。函数每次调用先将 CAN1/CAN2 的 `0x200`、`0x1FF` 四个 8 字节发送缓存清零，再将同一控制组中最多四台已登记电机的 `currentSet` 按 C610 协议写入：每台占两个字节，高字节在前。`0x201~0x204` 对应 `0x200`，`0x205~0x208` 对应 `0x1FF`；未登记的槽位保持零电流。

保护逻辑：写入前按各电机的 `maxCurrent` 限制 `currentSet`，避免超过 M2006 当前设置的 `±10000` 指令范围。只有存在已登记电机的控制组才调用 `CAN_Transmit()`。`sendControlData()` 的返回类型由 `void` 改为 `HAL_StatusTypeDef`：所有需要发送的帧成功提交时返回 `HAL_OK`，任一邮箱已满且无错误时返回 `HAL_BUSY`，任一发送错误时返回 `HAL_ERROR`。即使一组发送失败，函数仍会尝试发送其余独立控制组。

调试记录：首次构建报错 `CAN_Transmit was not declared in this scope`。原因是 `DJIMotorHandler.cpp` 已使用板级 CAN 封装，但只包含了 CubeMX 的 `can.h`，没有包含其声明所在的 `bsp_can.hpp`。补充这一直接依赖后重新构建成功。

验证目的：确认控制帧分组、字节序、限幅和发送状态汇总逻辑能够由 ARM C++ 工具链编译和链接。未调用 `CAN_Init()`、未创建电机对象、未调用 `sendControlData()`，也未连接 C610/M2006；CAN 邮箱状态、报文 ID、字节序和实际电流输出均留待统一真机验证。

构建操作：

```text
cmake --build build/codex-m2006-batch-a
```

终端结果：

```text
Building CXX object .../modules/src/DJIMotorHandler.cpp.obj
[2/2] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：C610 控制帧的软件打包与发送入口已完成并通过 ARM 交叉编译。PID、`M2006::setOutput()`、CAN 接收分发与线程入口仍未实现，因此固件中尚不存在自动产生或发送电机控制电流的调用链。

## 2026-10-10：M2006 批次 C——控制模式与在线判定（软件实现）

开发：完成 `M2006::setOutput()` 的三种已有控制模式。松弛模式会清除速度/位置 PID 的历史状态并将 `currentSet` 置零；速度模式以 `speedSet` 为参考值、`motorFeedback.speedFdb` 为反馈值，运行速度 PID 后写入 `currentSet`；位置模式先以 `positionSet` 与 `positionFdb` 运行位置 PID，位置 PID 的输出作为速度 PID 的参考值，再由速度 PID 得到电流命令。所有模式最终都通过本地限幅写入 `currentSet`；NaN 结果会安全地转换为零电流。

参数边界：M2006 构造函数现将速度 PID 的总输出和积分输出上限设为 `maxCurrent = 10000`，确保未来填写经真机整定的速度 PID 增益后，输出不会越过当前电流指令上限。PID 的 `kp/ki/kd` 仍保持零；位置 PID 的输出上限也保持原值，不凭空猜测速度上限或控制参数。因此，本批完成的是控制结构和安全限幅，不代表已经取得可用的真机 PID 参数。

在线保护：完成 `M2006::AliveCheck()` 和 `DJIMotorHandler::AllMotorAliveCheck()`。每次反馈解析成功都会递增 `AliveFlag`；在线检查将其与上次保存的 `Pre_AliveFlag` 比较。计数未增长时，电机标记为离线，并强制切换到松弛模式、清零电流；计数增长时标记为在线。Handler 会遍历 CAN1/CAN2 的全部已登记槽位并调用各电机的检查函数。

验证目的：确认 M2006 对 PID 库的调用、虚函数在线检查和 Handler 遍历逻辑可由 ARM C++ 工具链编译和链接。未设置实际 PID 增益，未接入 CAN 接收分发或业务线程，未产生真实电机控制帧，也未进行真机测试。

构建操作：

```text
cmake --build build/codex-m2006-batch-a
```

终端结果：

```text
Building CXX object .../modules/src/M2006.cpp.obj
Building CXX object .../modules/src/DJIMotorHandler.cpp.obj
[3/3] Linking CXX executable board.elf
RAM:   36968 B / 128 KiB  (28.20%)
FLASH: 26312 B / 1 MiB   (2.51%)
```

结论：M2006 的松弛、速度、位置—速度双环控制结构，以及 Handler 的统一在线检查已完成并通过 ARM 交叉编译。实际 PID 参数、CAN 接收分发和控制线程仍未完成；在这些调用链建立前，代码不会自动驱动电机。

## 2026-10-10：线程阶段 1——IMU 线程最小实现（软件）

开发：在 `threads/src/alivethread.cpp` 中创建 BMI088 对象和 IMU 线程。线程启动后执行 BMI088 配置与加速度计/陀螺仪芯片 ID 检查；初始化成功后周期读取加速度、温度和角速度，并检查数据状态。每轮通过 `tx_thread_sleep(1)` 让出 CPU。线程栈从 ThreadX byte pool 分配，初始配置为 1024 字节、优先级 10。`App_ThreadX_Init()` 调用 `Threads_Init()` 创建线程，线程源文件和头文件路径已加入 `board/CMakeLists.txt`。

范围说明：本步只完成 IMU 线程的初始化与采样。传感器数据目前保存在 BMI088 对象中，跨线程安全读取接口尚未建立；Alive 信号量监控、Control 电机线程和 LED 状态提示留待后续步骤。优先级与栈大小是初始软件配置，后续仍需结合运行情况检查。

验证目的：确认 ThreadX 启动入口、C/C++ 声明链接、byte pool 栈分配及 BMI088 线程代码可由 ARM 工具链编译并链接。未进行真机验证。

构建操作：

```text
cmake -S board -B C:\Users\Lenovo\AppData\Local\Temp\rm27-thread-step1-c52c71135abe43feaf7b924a3b3ce266 -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_TOOLCHAIN_FILE=D:\embedded\rm27_training_framework_hw\board\cmake\gcc-arm-none-eabi.cmake"
cmake --build C:\Users\Lenovo\AppData\Local\Temp\rm27-thread-step1-c52c71135abe43feaf7b924a3b3ce266
git diff --check
```

终端结果：`[240/240] Linking CXX executable board.elf`；RAM 使用 37672 B / 128 KB（28.74%），FLASH 使用 37988 B / 1 MB（3.62%）。增量复查显示 `ninja: no work to do`；`git diff --check` 通过。Git 提示新文件会由 LF 转换为 CRLF，是行尾格式提示，不是构建错误。

结论：IMU 线程的软件启动与编译链已完成；尚未确认真实 BMI088 通信、栈余量或周期表现。下一步先补齐 IMU 数据的线程间安全读取方式，再实现 README 要求的 CAN 电机控制线程。

## 2026-10-10：线程阶段 1B——IMU 数据线程间读取

开发：新增 `ImuDataSnapshot`，集中保存最近一次加速度（含温度）、角速度、初始化状态及两类数据有效状态。IMU 线程在初始化检查后发布状态，每轮采样和数据检查后更新快照。`Threads_GetIMUData()` 通过 ThreadX 互斥锁复制一份一致的快照，调用方不会直接读取正在被 IMU 线程更新的对象。互斥锁在线程创建前建立；栈分配或线程创建失败时会清理已创建资源。

调试记录：首次构建发现 ThreadX 创建接口将对象名称声明为可写字符指针，字符串常量触发两条 `-Wwrite-strings` 警告。将名称改为静态 `CHAR` 数组后重新构建，警告消失。

验证目的：确认快照接口、互斥锁、线程初始化错误清理和 C/C++ 接口链接可通过 ARM 交叉编译。仅做软件构建检查，未做真机测试或运行时并发测试。

构建操作：

```text
cmake -S board -B C:\Users\Lenovo\AppData\Local\Temp\rm27-thread-imu-share-ffee8d1c7ba0496cb343077286352b99 -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_TOOLCHAIN_FILE=D:\embedded\rm27_training_framework_hw\board\cmake\gcc-arm-none-eabi.cmake"
cmake --build C:\Users\Lenovo\AppData\Local\Temp\rm27-thread-imu-share-ffee8d1c7ba0496cb343077286352b99
git diff --check
```

终端结果：首次完整构建 `[240/240] Linking CXX executable board.elf`；修正名称警告后增量构建 `[2/2] Linking CXX executable board.elf`，未再出现编译警告。最终 RAM 使用 37784 B / 128 KB（28.83%），FLASH 使用 42244 B / 1 MB（4.03%）；`git diff --check` 通过。Git 仍提示 LF/CRLF 行尾转换，不影响构建。

结论：IMU 数据快照和线程安全读取接口已完成并通过 ARM 构建；真机读取、锁竞争与栈余量尚未验证。下一阶段可在此接口基础上接入 Control 线程。

## 2026-10-11：线程阶段 2——Control 通用 CAN 接收

开发：在现有 `threads/src/alivethread.cpp` 中增加 Control 线程。CAN 中断回调只复制标准数据帧并以非阻塞方式放入 16 帧 ThreadX 队列；Control 线程负责取帧并发布最近一帧快照，包含 CAN 句柄、标准 ID、DLC 和数据。`Threads_GetCANRxSnapshot()` 可在线程上下文安全读取快照，并提供接收数、队列丢帧数及 CAN 初始化状态。`CAN_Init()` 现在返回 HAL 状态，供 Control 记录初始化结果。

范围说明：当前只搭建通用接收链路，不解析电机 ID、不调用 M2006 Handler、不运行 PID，也不发送 CAN 控制帧；待确认电机所在总线和反馈 ID 后再接入电机反馈与控制。队列满时新帧会丢弃并计数。

验证目的：确认 CAN 初始化状态接口、ISR 到线程的队列交接、线程快照接口及 IMU/Control 线程共同创建可通过 ARM 交叉编译。未做真机 CAN 收发或运行时队列测试。

构建操作：

```text
cmake -S board -B C:\Users\Lenovo\AppData\Local\Temp\rm27-thread-control-generic-7d4be790c4be4f04a31c9d02c63f0b41 -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_TOOLCHAIN_FILE=D:\embedded\rm27_training_framework_hw\board\cmake\gcc-arm-none-eabi.cmake"
cmake --build C:\Users\Lenovo\AppData\Local\Temp\rm27-thread-control-generic-7d4be790c4be4f04a31c9d02c63f0b41
git diff --check
```

终端结果：`[240/240] Linking CXX executable board.elf`；RAM 使用 38480 B / 128 KB（29.36%），FLASH 使用 47704 B / 1 MB（4.55%）。最终增量复查 `[4/4] Linking CXX executable board.elf`，RAM 使用 38480 B，FLASH 使用 47704 B；`git diff --check` 通过。Git 提示 LF/CRLF 行尾转换，不影响构建。

结论：Control 的通用 CAN 接收软件框架已完成并通过 ARM 构建；电机反馈映射、双环控制、真机收发和队列负载仍待后续确认与验证。

## 2026-10-11：线程阶段 3——Alive 心跳监控与状态灯

开发：在现有 `threads/src/alivethread.cpp` 中加入 Alive 线程，以及 IMU、Control 两个心跳信号量。IMU 和 Control 线程在循环中刷新各自的心跳；Alive 每 10 个 ThreadX tick 读取一次心跳和线程初始化快照。两条线程均初始化成功且本监控周期内都有心跳时亮绿灯，否则亮红灯，蓝灯保持关闭。保留单一线程 CPP，没有新增线程源文件。

验证目的：检查 Alive 线程、信号量、状态快照和 GPIO 状态灯能够与现有 IMU/Control 代码一起通过 ARM 交叉编译链接。当前 LED 电平按高电平点亮编写，实际灯的有效电平和运行状态尚未在真机确认。

构建操作：

```text
cmake -S board -B C:\Users\Lenovo\AppData\Local\Temp\rm27-thread-alive-20261011-a13f -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_TOOLCHAIN_FILE=D:\embedded\rm27_training_framework_hw\board\cmake\gcc-arm-none-eabi.cmake"
cmake --build C:\Users\Lenovo\AppData\Local\Temp\rm27-thread-alive-20261011-a13f
git diff --check
```

终端结果：完整构建 `[240/240] Linking CXX executable board.elf`；RAM 使用 38768 B / 128 KB（29.58%），FLASH 使用 50580 B / 1 MB（4.82%）。`git diff --check` 通过。未进行真机测试。

结论：README 要求的 Alive 软件监控框架已完成并通过 ARM 构建；RGB 极性、实际灯效与线程运行时表现待统一真机验证。

## 2026-10-11：线程阶段 4——已登记电机的 Control 软件闭环

开发：在 `threads/src/alivethread.cpp` 的 Control 线程中接入 `DJIMotorHandler`。每个固定控制周期最多取出 16 帧 CAN 队列数据；原始帧仍先发布到既有快照，只有 DLC 为 8 且标准 ID 在 `0x201~0x208` 的帧才会按 `ID - 0x201` 分发给 `updateFeedback()`。随后同一 Control 线程执行已登记电机的在线检查、`setOutput()` 和 `sendControlData()`。CAN 中断仍只负责非阻塞投递队列，避免在中断中执行反馈解析、PID 或 CAN 发送。

设计说明：当前仓库尚未配置实际电机所在 CAN 总线、反馈 ID 或 PID 参数，因此本批不创建或登记虚假的 M2006 对象，也不写死 `CAN1 + 0x201`。未来上层在确认实物参数后调用已有 `registerMotor()` 即可进入这条闭环；未登记任何电机时，Handler 不发送控制帧。电机在线检查由 Control 线程独占执行，避免与 Alive 线程并发修改同一电机的反馈、模式和电流状态。

验证目的：检查新线程调用在语法层面能正确连接 CAN BSP、DJI Handler 与 M2006 接口；确认短帧和无关 ID 不会进入电机反馈解析，且默认没有实际电机配置时不会产生电机电流输出。本批的完成标准是软件调用链接通，不代表 PID 参数、CAN 总线和电机实际运动已验证。

构建操作：

```text
cmake -S board -B C:\Users\Lenovo\AppData\Local\Temp\rm27-thread-motor-loop-20261011 -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_TOOLCHAIN_FILE=D:\embedded\rm27_training_framework_hw\board\cmake\gcc-arm-none-eabi.cmake"
C:\mingw64\bin\g++.exe -std=c++11 -fsyntax-only -DUSE_HAL_DRIVER -DSTM32F407xx -Iboard/Core/Inc -Iboard/Drivers/STM32F4xx_HAL_Driver/Inc -Iboard/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -Iboard/Drivers/CMSIS/Device/ST/STM32F4xx/Include -Iboard/Drivers/CMSIS/Include -Iboard/Middlewares/ST/threadx/common/inc -Iboard/Middlewares/ST/threadx/ports/cortex_m4/gnu/inc -Ibsp/include -Ilibs/include -Imodules/include -Ithreads/include threads/src/alivethread.cpp
git diff --check
```

终端结果：ARM CMake 配置阶段被本机 `arm-none-eabi-gcc.exe` 的 Windows “系统 DLL 重定位”错误阻断，未开始编译本次源码，属于当前交叉编译器环境问题而非源码诊断。随后使用本机 C++ 编译器进行仅语法检查，`threads/src/alivethread.cpp` 通过；输出的两条 CMSIS 指针位宽警告来自以 x64 主机检查 Cortex-M4 头文件，不是 ARM 目标构建警告。`git diff --check` 通过；Git 的 LF/CRLF 提示未影响检查结果。

结论：已登记电机的反馈分发、PID 输出、CAN 控制帧发送和掉线检查已在 Control 线程的软件调用链中接通；但 ARM 完整构建与真机 CAN/PID/电机行为均尚未验证。待修复本机 ARM 工具链后必须重新执行完整构建，真机验证继续按 `docs/hardware_validation.md` 的 M2006 步骤进行。
