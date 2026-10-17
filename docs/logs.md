# 调试与开发日志

## Obsidian 调试记录（原始记录）

| 阶段               | 你要做的事                           | 要获得的证据                  | 这一阶段能说明什么            |
| ---------------- | ------------------------------- | ----------------------- | -------------------- |
| 1. 确认调试环境        | 确认 VS Code 的构建、调试工具，以及调试器到板子的连接 | 明确用什么编译、用什么连接 STM32     | 调试所需的路径和工具已经明确       |
| 2. 编译当前代码        | 从当前源码生成用于调试的固件                  | 编译、链接成功，明确本次生成的 ELF 路径  | 代码能生成固件；还不代表硬件正常     |
| 3. 下载并进入调试       | 将本次固件下载到板子，让程序停在 `main()`       | VS Code 显示程序暂停在对应源码位置   | 下载和基本调试链路已经打通        |
| 4. 确认 IMU 线程运行   | 在 IMU 线程入口设置断点，继续运行             | 程序命中 `ImuThreadEntry()` | 程序已经走到 BMI 的执行入口     |
| 5. 验证 BMI 通信与初始化 | 观察初始化过程、两部分芯片 ID 和错误标志          | 读到的 ID 与预期一致，并检查初始化状态   | 获得 BMI 通信正常的初步证据     |
| 6. 验证静止数据        | 将板子静置，观察加速度、角速度和温度              | 记录数值、单位、波动情况            | 判断数据是否具有合理的物理意义      |
| 7. 验证运动响应        | 有控制地倾斜或转动板子，比较前后数据              | 数据变化与动作相符               | 判断轴向、符号和数据更新是否合理     |
| 8. 整理结果          | 记录条件、操作、观测值和判断依据                | 一份可重复的实验记录              | 下次能复现，也能解释为什么认为通过或失败 |

配置：CMake 读取工程设置，生成构建规则
  ↓
编译、链接：把 C/C++ 源码变成 board.elf 固件
  ↓
烧录：通过 ST-Link，把固件写入 STM32 的 Flash

进入正确的工程目录
Set-Location D:\embedded\rm27_training_framework_hw\board

配置 Debug 构建  cmake --preset Debug
编译并链接固件   cmake --build --preset Debug

烧录
& "D:\OpenOCD\bin\openocd.exe" -s "D:/OpenOCD/share/openocd/scripts" -f "interface/stlink.cfg" -c "transport select swd" -f "target/stm32f4x.cfg" -c "adapter speed 1000" -c "program D:/embedded/rm27_training_framework_hw/board/build/Debug/board.elf verify reset exit"

在.vscode 下面写了 launch.json
以下内容：只用配置一次
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "C板：停在 main",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "openocd",
            "cwd": "${workspaceFolder}",
            "executable": "${workspaceFolder}/board/build/Debug/board.elf",
            "armToolchainPath": "D:/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin",
            "serverpath": "D:/OpenOCD/bin/openocd.exe",
            "searchDir": [
                "D:/OpenOCD/share/openocd/scripts"
            ],
            "configFiles": [
                "interface/stlink.cfg"
            ],
            "openOCDLaunchCommands": [
                "transport select swd",
                "source [find target/stm32f4x.cfg]",
                "adapter speed 1000"
            ],
            "runToEntryPoint": "main"
        }
    ]
}

再`ImuThreadEntry` 的 bmi088.Config() 打断点成功
在左侧“监视”中添加并展开：bmi088.self_tes
![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015102014.png>)
bmi088.VerifyAccChipID();
![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015102954.png>)
BMI 初始化阶段结束后，加速度计、陀螺仪 ID 错误标志及初始化错误标志均为 `false`。本次 ID 校验通过，配置写入未报告失败；数据有效性待验证。

![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015104850.png>)
开始更新了 出现的问题是加速度的数据没有发生变化 但temp和陀螺仪的数据在变
log1： 加速度计使能寄存器状态和预期不一致
下一步排查一下原始的加速度数据读取有没有问题 以及是不是转换的函数写错了 还是没有成功return
1 断点打在读取之后
![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015105043.png>)
现象是出现了很多0值 why？
![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015105313.png>)
下一步检查 加速度测量是否真的启用了
BMI088 的“能读到芯片 ID”和“正在输出加速度”是不同条件。手册中有两个直接控制加速度采集的寄存器：
|寄存器|地址|当前程序希望设置的值|含义|
|---|---|---|---|
|`ACC_PWR_CTRL`|`0x7D`|`0x04`|开启加速度计|
|`ACC_PWR_CONF`|`0x7C`|`0x00`|进入活动模式，允许采集|
检查config里面到底有没有配置好寄存器 我们在线程里面直接在config后面去开启
uint8_t acc_pwr_ctrl_readback = 0xFF;
uint8_t acc_pwr_conf_readback = 0xFF;

HAL_StatusTypeDef acc_pwr_ctrl_status = bmi088.ReadReg(
    BMI088::BMI088_CS_ACC, ACC_PWR_CTRL_ADDR, &acc_pwr_ctrl_readback, 1);

HAL_StatusTypeDef acc_pwr_conf_status = bmi088.ReadReg(
    BMI088::BMI088_CS_ACC, ACC_PWR_CONF_ADDR, &acc_pwr_conf_readback, 1);
预期结果：acc_pwr_ctrl_readback  应该是0x04 表示开启
          acc_pwr_conf_readback 0x00 活动模式
![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015110155.png>)
分析：
	现象：加速度 XYZ 持续为零；原始六字节均为零。
	配置读回：
	  ACC_PWR_CTRL：HAL_OK，读回 0x00，期望 0x04。
	  ACC_PWR_CONF：HAL_OK，读回 0x00，符合预期。
	判断：发现加速度计使能状态与期望不一致。
	待查：开启写入是否生效，以及之后是否发生状态变化。
读取没有问题 看看是不是没有成功写入

贼tm神奇啊 重新写入一遍之后 就可以了
![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015114919.png>)

woc真傻逼 ：
Bosch 官方驱动的 `bmi08a_soft_reset()`，在发送加速度计软复位命令、等待完成后，**会额外进行一次 Chip ID 的 dummy SPI 读取**
软复位：复位到默认值状态
dummy spi：BMI 的加速度计初始默认使用 I2C 总线，需要片选切换到 SPI。

所以说第一次是 dummy SPI 读取，应该读不到，所以我一开始要自己写一个 dummy，然后省掉那一次读取。

加入软复位后的 dummy 访问后，第一次使能读回正确，你也已经观察到加速度和陀螺仪数据恢复
[log1 done]

然后把 BMI 的数值作为一个物理上的计算，看看有没有偏离它的物理意义。

| 组别  | 摆放状态 | \(a_x\)（m/s²） | \(a_y\)（m/s²） | \(a_z\)（m/s²） | 合成大小 \(\lvert\mathbf{a}\rvert\)（m/s²） | 相对误差   | 温度     |
| --- | ---- | ------------- | ------------- | ------------- | ------------------------------------- | ------ | ------ |
| 1   | 平放   | 0.166882      | -0.039478     | 9.573303      | 9.574839                              | -2.30% | 27.875 |
| 2   | 平放   | 0.161499      | -0.107666     | 9.602014      | 9.603975                              | -2.00% | 28.375 |
| 3   | 平放   | 0.139966      | 0.028711      | 9.593041      | 9.594105                              | -2.10% | 28.375 |
| 4   | 立起   | -0.538330     | -9.605603     | -0.385803     | 9.628409                              | -1.75% | 28.250 |

加速度计静态数据初步合理，约 2% 的偏差暂时接受并记录，后续按使用需求决定是否标定

现在是陀螺仪的数据，去看一下合不合理，物理意义
通过顺时针和逆时针旋转看 X 轴的变化，动态响应的方向是正确的，但没有做更精细的判断。

gpt给我了一段DMI的验收标准
![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015162723.png>)

单电机控制：
![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015163032.png>)


BMI 收尾与记录
    ↓
线程、内存的基础检查
    ↓
CAN 与单电机完整验证
    ↓
其他外设补验
    ↓
联合运行、栈使用复查、最终整理

BMI 收尾
- **启动可重复性**：确认每次复位、重新上电都能正常初始化，成功不依赖上一次调试留下的状态。
- **脱离断点连续运行**：确认程序按正常速度运行时也能持续采集，不依赖单步操作额外提供的等待时间。
- **通信失败识别**：确认读取出错时，程序能发现，并明确告诉使用者“这份数据无效”。
- **跨线程提供数据**：确认 BMI 读到的结果能正确传到其他线程，完成从采集到使用的完整流程。

程序自主运行后，`alive` 线程取得的 IMU 快照显示初始化成功、数据有效
这个地方需要等待一会儿，因为一开始的初始化芯片都需要花时间等待。等 30 秒后再添加断点，就可以看到初始化的结果和 snapshot。
![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261015165440.png>)
断电把 launch.json 改成了 attach 模式，附着模式就不让调试器帮助 MCU 复位，然后发现是可以的，跑了几次
快速体检 让数据从头走到尾 跑通找范围 再深入找原因

错误判断：
读取加速度 → 判断读取是否成功 → 设置错误标志
          → 更新共享快照 → 其他线程读取快照
现在错误标签前面改动一下 然后看看传下去的几个坎有没有发生改变
**`ACC_DATA_ERR = true`**    `acceleration_valid = false`    成功done

验证两段：
加速度失败 → 共享快照标为无效 → alive 取到“无效”
温度读取失败 → 温度错误被记录 → 快照标为无效 → alive 取到“无效”



复现问题 写清预期和实际 缩小范围 观察关键值 主要假设 做最小实验 确认修好
脑子留住：要解决什么 怎样算成功 系统大致怎么流 当前原因 用什么证据判断修好了
AI 找位置 列可能原因 建议断电 小实验 改代码 补测试



我现在放权让你去做这个调试，你可以去终端里面，在监视变量里面去写，帮我把温度这个地方调好就行。不要改我最终的代码，不要提交。
你需要返回的是每一步：

1. 你为什么做这个决定？
2. 为什么用这个实验？
3. 看了哪些变量？为什么看这些变量？
4. 真实的结果是什么样的？
5. 根据这个结果，你有没有调整这个实验？
6. 成功了没有？
7. 最后验收标准是什么？


请回答我以下几个问题：
1. 温度数据从哪里产生，最后被哪个线程使用？讲一下有哪些函数（在什么文件里）与温度数据的读取、写入、转移有关，最后被用在哪个线程，未来用来处理什么问题
2. 为什么加速度失败会影响本轮温度的有效性？
3. 正常、温度失败、加速度失败、下一轮恢复时，各个有效标志（看什么变量）应该是什么？
4. 每个结论应该在哪个动作完成之后观察？
5. 这次用调试器改返回值证明了什么，又没有证明什么？


| 步骤                                                                                                                           | 为什么做、用了什么实验                                                                                                                                                                                                                                                                               | 看哪些变量，实际看到了什么                                                                                                                                                                               | 是否调整、结果                           |
| ---------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------- |
| 1. 确认程序版本                                                                                                                    | 先只读比对板上 Flash 与当前 ELF，避免拿新代码的变量名解释旧程序。                                                                                                                                                                                                                                                    | 从 `0x08000188` 起发现差异。烧录当前 ELF 后，OpenOCD 报告 `Verified OK`。                                                                                                                                   | 发现版本不一致，因此先烧录再调试。通过。              |
| 2. 正常温度读取                                                                                                                    | 在快照字段赋值完成后停住，确认传感器读数能进入共享快照。                                                                                                                                                                                                                                                              | `TEMP_DATA_ERR=false`、`acc_data.temperature=30.75°C`、`imu_data_snapshot.temperature_valid=true`；初始化和加速度有效标志也为 `true`。                                                                       | 无需调整。通过。                          |
| 3. 模拟温度通信失败及恢复                                                                                                               | 在温度 `ReadReg` 返回后，仅将这一次的返回值从 `HAL_OK(0)` 改为 `1`，检验失败分支及下一轮恢复。                                                                                                                                                                                                                             | 失败当轮：`TEMP_DATA_ERR=true`、温度 `0`、`temperature_valid=false`，而加速度仍有效。继续运行后：温度 `30.875°C`，错误标志恢复为 `false`，有效标志恢复为 `true`。                                                                      | 无需调整。通过。                          |
| 4. 模拟加速度读取失败                                                                                                                 | 温度读取位于加速度读取之后；若前者被跳过，需要防止上一次温度成功状态被误用。                                                                                                                                                                                                                                                    | 将一次加速度 `ReadReg` 返回值改为失败后，`ACC_DATA_ERR=true`、加速度和温度数据清零。虽然旧的 `TEMP_DATA_ERR` 仍为 `false`，快照的 `temperature_valid=false`。                                                                     | 证实快照中同时检查加速度状态是必要的。通过。            |
| 5. 检查另一线程取得快照                                                                                                                | 在 Alive 线程的 `Threads_GetIMUData()` 调用后观察局部变量，并临时改写共享标志验证复制。                                                                                                                                                                                                                               | 正常时，局部快照有 `30.875°C` 且 `temperature_valid=true`。复制前把共享标志改为 `false`，单步越过调用后，调用成功且局部标志为 `false`。继续运行后两处均恢复为 `true`，温度为 `30.625°C`。                                                            | 第一次断在调用语句上，复制尚未发生；已将断点移到下一行重做。通过。 |
| 要看的变量                                                                                                                        | 谁写它、写了什么                                                                                                                                                                                                                                                                                  | 断点放在哪里看                                                                                                                                                                                     |                                   |
| `bmi088.AccReadOk`                                                                                                           | [ReadAccData() (line 354)](/D:/embedded/rm27_training_framework_hw/modules/src/BMI088.cpp:354) 第 360 行，根据三轴加速度的 `ReadReg()` 返回值写入                                                                                                                                                         | [BMI088.cpp 第 361 行 (line 361)](/D:/embedded/rm27_training_framework_hw/modules/src/BMI088.cpp:361)：第 360 行已执行，可判断本轮加速度 SPI 读取是否成功                                                          |                                   |
| `bmi088.TemperatureReadOk`、`bmi088.self_test.TEMP_DATA_ERR`                                                                  | [ReadAccTemperature() (line 395)](/D:/embedded/rm27_training_framework_hw/modules/src/BMI088.cpp:395) 在温度读取失败时分别写 `false`、`true`；成功时写 `true`、`false`                                                                                                                                      | 正常或仅温度失败时，停在 [ReadAccData() 第 372 行 (line 372)](/D:/embedded/rm27_training_framework_hw/modules/src/BMI088.cpp:372)：第 371 行的温度函数调用已结束。`TemperatureReadOk` 是对象私有成员，重点看可公开观察的 `TEMP_DATA_ERR` |                                   |
| `bmi088.acc_data.temperature`                                                                                                | `ReadAccTemperature()` 通过 `temp` 指针写入摄氏度，失败写 `0`；`ReadAccData()` 保存整份 `acc_data`。若三轴读取失败，`ReadAccData()` 直接把整份数据清零                                                                                                                                                                        | 正常或仅温度失败看第 372 行；三轴失败则看 [ReadAccData() 第 365 行 (line 365)](/D:/embedded/rm27_training_framework_hw/modules/src/BMI088.cpp:365)，因为这条路径提前返回，**不会走到第 372 行**                                   |                                   |
| `bmi088.self_test.ACC_DATA_ERR`                                                                                              | [VerifyAccData() (line 191)](/D:/embedded/rm27_training_framework_hw/modules/src/BMI088.cpp:191) 根据 `AccReadOk` 和三轴数值检查结果写入                                                                                                                                                               | 停在 [ImuThreadEntry() 第 115 行 (line 115)](/D:/embedded/rm27_training_framework_hw/threads/src/alivethread.cpp:115)：第 114 行的 `VerifyAccData()` 已执行                                            |                                   |
| `imu_data_snapshot.acceleration.temperature`、`initialized`、`acceleration_valid`、`temperature_valid`、`angular_velocity_valid` | [PublishIMUData() (line 75)](/D:/embedded/rm27_training_framework_hw/threads/src/alivethread.cpp:75) 第 80–85 行复制数据并计算标志。其中 `temperature_valid = !INIT_ERR && !ACC_DATA_ERR && !TEMP_DATA_ERR`                                                                                             | 停在 [第 87 行 (line 87)](/D:/embedded/rm27_training_framework_hw/threads/src/alivethread.cpp:87)：所有快照字段已写完；此时互斥锁尚未释放，适合**观察共享快照**                                                              |                                   |
| Alive 线程局部的 `imu_snapshot.acceleration.temperature`、`imu_snapshot.temperature_valid`、`imu_status_valid`                      | [Threads_GetIMUData() (line 424)](/D:/embedded/rm27_training_framework_hw/threads/src/alivethread.cpp:424) 用 `*snapshot = imu_data_snapshot` 复制整份快照；[AliveThreadEntry() (line 253)](/D:/embedded/rm27_training_framework_hw/threads/src/alivethread.cpp:253) 根据调用是否成功写 `imu_status_valid` | 停在 [AliveThreadEntry() 第 254 行 (line 254)](/D:/embedded/rm27_training_framework_hw/threads/src/alivethread.cpp:254)：第 253 行调用已完成。**不要停在第 253 行就判断局部快照**                                     |                                   |
温度有效性链路已经完成真机验证；陀螺仪运行期失败和初始化 Chip ID 失败尚未做同等级覆盖。

现在对陀螺仪和chip id做验证

| 阶段、数据流与变量写入                                                              | 实验和观察位置：停下时执行到哪一步                                                                                                                               | 预期                                                                          | 实际观察                                                                                                                                                                      | 调整、结论与边界                                                                                              |
| ------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| 6. 陀螺仪正常基线：`ReadGyroData()` → `VerifyGyroData()` → 快照                    | 复位后第二次命中发布第 87 行；第一次发布只有初始化状态，尚未采样                                                                                                              | `GYRO_DATA_ERR=false`、角速度有效                                                 | 三轴约 `{0.0117, -0.00426, 0}`，`GYRO_DATA_ERR=false`、`angular_velocity_valid=true`                                                                                           | 区分了“刚初始化”和“已完成一轮采样”                                                                                   |
| 7. **单次陀螺仪读取失败**：SPI 返回 → `GyroReadOk` → `GYRO_DATA_ERR` → 共享快照          | 停在 `BMI088.cpp:381` 的 `ReadReg()` **返回后、`GyroReadOk` 赋值前**，读到真实返回值 `0`，仅将本次返回寄存器改为 `1`；再停在发布第 87 行                                              | `GyroReadOk=false`、`GYRO_DATA_ERR=true`、三轴清零、`angular_velocity_valid=false` | 四项均如此；同轮 `acceleration_valid=true`、`temperature_valid=true`                                                                                                               | 第一次尝试在中间单步后未命中发布断点，CPU 停在 HAL SPI 等待循环；原因未被证明。复位后改为只在返回点和发布点停住，取得完整结果，不能把第一次卡住当作代码失败                  |
| 8. 陀螺仪失败到 Alive，以及恢复                                                     | 为避免单次失败在 Alive 下次读取前被覆盖，只在调试会话中连续改写陀螺仪读取返回值；停在 Alive 第 254 行。删除注入断点后再次观察                                                                        | 失败局部快照无效；撤销后恢复                                                              | 失败时 `imu_status_valid=true`，Alive 局部三轴 `{0,0,0}`、`angular_velocity_valid=false`，同时 `GyroReadOk=false`、`GYRO_DATA_ERR=true`；撤销后局部角速度非零、有效标志 `true`，错误标志 `false`            | **跨线程失败与恢复通过**。连续故障仍是调试器模拟，未验证真实总线故障                                                                  |
| 9. **陀螺仪 Chip ID 读取状态失败**：`VerifyGyroChipID()` → `INIT_ERR` → 快照 → Alive | 启动时停在 `BMI088.cpp:184` 的 `ReadReg()` **返回后、局部 `status` 赋值前**；真实返回 `0`、ID `0x0F`，仅把返回寄存器改为 `1`。在 `ImuThreadEntry():107`、发布第 87 行、Alive 第 254 行观察 | 陀螺仪 ID 报错、初始化失败，Alive 取得未初始化快照；不应在普通采样轮自动恢复                                 | `GYRO_CHIP_ID_ERR=true`、`ACC_CHIP_ID_ERR=false`、`INIT_ERR=true`；共享及 Alive 快照 `initialized=false`、数据有效标志全 `false`，而 `imu_status_valid=true`。撤销注入后继续运行，状态仍为失败               | **失败传播通过**。复位后真实返回 `0`、ID `0x0F`，`INIT_ERR=false`，下一次采样 `angular_velocity_valid=true`。这验证了为何恢复需要重新初始化 |
| 10. **加速度计 Chip ID 值不匹配及复位恢复**                                           | 启动时停在 `BMI088.cpp:174` 的 `ReadReg()` **返回后、校验前**；真实返回 `0`、ID `0x1E`，仅把本次局部 `chip_id` 改为 `0`。同样观察检查结束、发布和 Alive 复制后                              | 加速度计 ID 报错；陀螺仪 ID 可以仍通过；复位后两者恢复                                             | `ACC_CHIP_ID_ERR=true`、`GYRO_CHIP_ID_ERR=false`、`INIT_ERR=true`；共享及 Alive 快照未初始化、数据无效。撤销注入并复位后，两颗 ID 分别重读为 `0x1E`、`0x0F`，错误标志均为 `false`；随后三类数据有效标志全为 `true`，Alive 也取得有效数据 | 两颗芯片的独立 ID 路径均有实测。加速度计这轮测试的是**值不匹配**，没有另行注入它的 SPI 返回失败                                                |


准备开始调单电机

先跑一下 最小可运行程序 看看CAN有没有对齐 能不能让电机驱动起来

电机保持松弛 在线程里面注册一个电机 CAN初始化成功后 注册到`CAN1/0x201
添加了[MotorConfig.cpp](D:/embedded/rm27_training_framework_hw/modules/src/MotorConfig.cpp) 保存测试状态和反馈结果 电机的信息注册在这里面

第一次可执行程序测试
Control 线程在计算完默认松弛输出、发送 CAN 帧之前调用它。用 GDB 显式触发后，程序先确认 `CAN1/0x201` 反馈在线，再发送约 **100 ms、指令值 1000** 的电流，随后持续发送零电流并锁定本次测试。GDB 等脉冲结束后再读取最高反馈转速、位置变化和最终电流；整个过程不接 PID。验收点是非零帧出现、反馈随之变化、测试结束后回到零电流。

最小开环控制链 ： 注册、发送、接收反馈、定时停止这一条最小链路跑通了
完成电机基础动作验收（）  PID部分

![调试截图](<../assets/logs/obsidian-debug/Pasted image 20261016143025.png>)

没毛

| 试验                | 实际反馈与现场观察                                                                                           | 控制帧记录                                                            | 停止后的零输出                                                                   | 异常情况                                     |
| ----------------- | --------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- | ------------------------------------------------------------------------- | ---------------------------------------- |
| 正向 `+500`，50 tick | 位置反馈 `+0.000021305 → +0.005411544 rad`；转速**绝对值**峰值 `54 rpm`，电流反馈原始值绝对峰值 `184`。你看到输出轴动了，但没看清方向。      | 脉冲期间 CAN1/`0x200` **软件打包缓存**：`01 F4 00 00 00 00 00 00`；提交 `5` 帧。 | `RELAX_MODE`，`currentSet=0`，当前发送缓存全零。                                     | 提交失败 `0`；CAN 丢帧 `0`，`hcan1.ErrorCode=0`。 |
| 反向 `-500`，50 tick | 位置反馈 `+0.000021305 → -0.007158577 rad`；转速**绝对值**峰值 `140 rpm`，电流反馈原始值绝对峰值 `166`。你判断运动方向应该与上次相反，为逆时针。 | 脉冲期间 CAN1/`0x200` **软件打包缓存**：`FE 0C 00 00 00 00 00 00`；提交 `5` 帧。 | `RELAX_MODE`，`currentSet=0`，当前发送缓存全零；停止后发送邮箱记录标准 ID `0x200`、DLC `8`、数据全零。 | 提交失败 `0`；CAN 丢帧 `0`，`hcan1.ErrorCode=0`。 |

是什么原因导致的 AI 团队去跨层级写这样子的代码

单开了一个motordebug

±0.5 rad/s 的纯 P 候选：Kp = 2800
±0.1 rad/s：不存在合格的纯 P 值
普通固件：暂不回填任何参数



### 2026-10-16：只接 C 板后的阶段验收

这轮只接 C 板，电机、C610、串口回环线、I²C 从设备和示波器都不在。先烧普通 `Debug` 固件，不开 `MotorDebug`，避免把调试入口当成正式功能。没有修改仓库源码。

| 验收项 | 这轮怎么验 | 实际观察 | 当前判断 |
| --- | --- | --- | --- |
| 最终构建、下载 | `cmake --preset Debug`、`cmake --build --preset Debug`，再用 OpenOCD 下载并校验 | 构建无报错；Flash 显示 `Verified OK`；普通 ELF 中没有 `MotorDebug` 和 GDB 电机试验符号 | **通过** |
| BMI088 Chip ID | 在两次 `ReadReg()` 返回后观察通信状态和 `chip_id` | ACC：`HAL_OK / 0x1E`；GYRO：`HAL_OK / 0x0F` | **通过** |
| BMI088 数据与单位 | 读取 IMU 共享快照；连续做 3 次硬件复位后再读 | 三次均 `initialized=true`，加速度、温度、角速度有效；温度约 `28.125 °C`；加速度单位 `m/s²`，角速度单位 `rad/s` | **通过**。本轮是复位复验，不等同于拔电后的真正重新上电 |
| BMI088 失败识别 | 复查前面的单次 SPI 失败、Chip ID 不匹配和跨线程快照记录 | 对应错误标志会置位，快照有效标志变为 `false`；恢复或重新初始化后的状态也已经记录 | **已有真机调试器注入证据**；真实断线故障仍没有再次制造 |
| RGB 三色灯 | 暂停 CPU 后分别切换 PH12、PH11、PH10，再读 GPIOH 输出寄存器，最后恢复健康状态绿灯 | 红、绿、蓝分别为 `ODR=0x1000 / 0x0800 / 0x0400`，恢复后为 `0x0800` | **GPIO 控制通过**；这次没有由人眼记录亮度和颜色照片 |
| ThreadX 心跳、互斥锁和快照 | 读取线程控制块、两把互斥锁、两个心跳信号量和 IMU/CAN 快照 | IMU、Control、Alive 都已运行并进入休眠态；运行次数持续增加；互斥锁观察时未被占用；IMU 快照一致；CAN 初始化成功 | **通过** |
| 线程栈和字节池 | 扫描 `0xEF` 栈填充值，并读取 byte pool | 三个线程栈都是 1024 B；估算最大占用：IMU 472 B、Control 496 B、Alive 392 B；32 KiB 字节池剩余 29664 B | **当前余量足够** |
| 主栈和堆 | 查看链接脚本和 map | 最小主栈 1024 B，最小堆 512 B；本次 ELF 的 `.bss=37328 B`，`._user_heap_stack=1540 B` | **配置已确认**；主栈运行期最高占用还没有测量 |
| 无电机时的安全状态 | 普通固件运行后读取 MotorControl 快照 | `online=false`、`RELAX_MODE`、`current_command=0`、控制帧 8 字节全零 | **通过**；电机撤下后没有运动输出 |
| 速度环、位置—速度双环 | 采用前面电机仍连接时的正式 MotorDebug 复验数据；本轮核对参数已经进入普通固件 | 速度环：`Kp=1800, Ki=5, Kd=0, max=500, max_i=150`；位置环：`Kp=3.5, Ki=0.002, Kd=0, max=0.3 rad/s, max_i=0.05 rad/s` | **历史真机验收通过，参数已进入普通固件**；本轮因电机撤下未重跑 |
| CAN ID、DLC、字节序与掉线 | 采用前面的电机真机记录；本轮只确认无反馈时保持松弛 | 反馈 `0x201`、控制 `0x200`、DLC 8；`+500 → 01 F4`，`-500 → FE 0C`；掉线后模式回松弛、控制帧归零 | **已有真机证据**；没有独立 CAN 分析仪抓总线波形 |
| UART1、UART6 收发 | 读取两个 HAL 句柄和错误码 | 两路均为 115200、8N1、TX/RX，`gState/RxState=READY`，`ErrorCode=0` | **只完成初始化检查**；没有回环线，实际发送、DMA 接收和回调未验收 |
| PWM 波形 | 读取 TIM8/TIM10 句柄和定时器参数 | 两个句柄均 READY；TIM8 参数对应 50 Hz、CCR1=1500；TIM10 参数对应约 33.6 kHz、CCR1=0；当前均未启动 | **未完成波形验收**；缺示波器，`TEMP_CTRL_ERR=true` 也说明温控 PWM 没有进入工作态 |
| I²C3 通信 | 读取句柄、寄存器和引脚电平 | I²C3 为 400 kHz，状态 READY、错误码 0、外设已使能；PA8/SCL、PC9/SDA 空闲时均为高 | **只完成初始化和总线空闲检查**；没有从设备，未验证地址应答和寄存器读写 |
| 底盘推导、SPI/I²C 文档、日志 | 检查 `docs/` | `spi.md` 和 `logs.md` 已有内容；`chassis.md`、`iic.md` 仍是空文件 | **SPI 与日志已有；底盘推导、I²C 文档未完成** |

前面“`Kp=2800` 候选、普通固件暂不回填”的记录是中途状态。最终速度环和双环已经完成后续复验，正式参数现在位于 `MotorControl`，普通固件会加载；`MotorDebug` 只负责调用同一套正式接口做试验。

#### 这轮之后还不能打勾的项目

1. 给 UART1、UART6 分别接回环或串口工具，实测阻塞/中断/DMA 发送与 DMA-to-idle 接收。
2. 用示波器测 PC6 的 50 Hz/1.5 ms 波形，以及 PF6 的 0%、10%、50% 占空比和停止状态。
3. 接入已知地址的 I²C3 从设备，确认 ACK、寄存器读写和通信失败返回值。
4. 填写 `docs/chassis.md` 和 `docs/iic.md`。
5. C 板真正断电再上电一次，复查 `0x1E/0x0F` 和三类数据有效标志；这一步需要现场拔插电源。
6. 若要把电机项作为“最终整机复验”，重新接回 C610 与 M2006，再跑松弛、正反速度、正反位置双环和掉线保护；当前保留的是撤线前的有效记录。

---

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

## 2026-10-15：BMI088 温度读取有效标志

在 `imu_error_t` 中增加默认报错的 `TEMP_DATA_ERR`，温度读取根据 SPI 结果更新该标志；IMU 共享快照增加 `temperature_valid`。初始化、加速度读取和温度读取均成功时才标记温度有效。ARM Debug 构建通过，尚未烧录验证温度读取失败和跳过读取场景。

## 2026-10-16：M2006 调试结果与分层整理

真机确认当前电机使用 CAN1、反馈标准 ID `0x201`、DLC 8，对应控制帧 ID `0x200`。松开状态下电流命令和控制帧数据均为零，试验期间 CAN 接收持续工作且记录的队列丢帧数为零。尚未使用独立 CAN 分析仪捕获非零总线帧。

开环试验使用 `+500` 与 `-500` 电流命令，软件打包结果分别为 `01 F4 00 00 00 00 00 00` 与 `FE 0C 00 00 00 00 00 00`。两种方向均产生了位置和速度响应，结束后回到松开和零电流。固定观察姿态下，正电流的持续运动对应正速度反馈和正位置变化；短脉冲的单个峰值样本受起动和采样时刻影响，不能单独用于判断方向。

速度单环以输出轴 `+0.1 rad/s` 为目标，控制周期 10 ms，`Ki=Kd=0`，电流命令限幅 `±500`，分别对 `Kp=2500、3000、3500、4000、4500` 进行了 300 ms 对比：

| Kp | 最大速度（rad/s） | 末段平均速度（rad/s） | 末段平均绝对误差（rad/s） | 现象 |
| ---: | ---: | ---: | ---: | --- |
| 2500 | 0.0087 | 0.0017 | 0.0983 | 输出约为 +250，几乎没有有效起动 |
| 3000 | 0.2443 | 0.0701 | 0.0785 | 延迟起动后过冲，电流正负交替 |
| 3500 | 0.2763 | 0.0468 | 0.0743 | 延迟起动后过冲，多次触及负限幅 |
| 4000 | 0.3607 | 0.1286 | 0.1133 | 过冲更大，连续触及负限幅 |
| 4500 | 0.2327 | 0.0960 | 0.0695 | 平均值接近目标，但瞬时波动明显 |

当前纯 P 参数均未通过稳速性能验收，`Kp=4500` 的平均值接近目标也不能视为已经稳定。低速静摩擦、反馈量化、控制周期和观察时长可能影响结果，但现有数据不足以确认主因，因此未将任何试验参数写成正式 PID 配置。

代码从提交 `406f31d` 重新整理：M2006 模块封装松开、开环电流、速度目标和 PID 配置；DJI Handler 统一执行已登记电机输出与 CAN 发送；MotorControl 保存并登记 CAN1/`0x201` 电机，并提供 PID 配置、速度目标、开环电流、松开和状态快照等正式接口；Control 线程只处理 CAN 队列并调用 MotorControl。GDB 触发、试验状态机和采样数据集中在独立 MotorDebug 模块，且只调用 MotorControl 的正式接口，不直接访问 M2006、PID 字段或 Handler。普通构建不编译 MotorDebug，只有显式使用 `MotorDebug` CMake preset 时才加入固件。正式 PID 默认值暂为零，待参数通过硬件验收后再写入 MotorControl 的默认配置。

普通 Debug 和 MotorDebug 两种 ARM 构建均通过。普通固件符号表中不存在 MotorDebug 入口或 GDB 试验变量；MotorDebug 固件中包含相应入口与 `can1_id201_open_loop_test`、`can1_id201_speed_loop_test`、`can1_id201_test_speed_kp`。本次只完成代码整理和构建，没有烧录；上述真机数据来自整理前的临时调试版本，不能替代整理后版本的硬件复验。

## 2026-10-16：M2006 速度 PID 真机整定

MotorDebug 的单次速度试验扩展到最长 5 s、500 个样本，并记录相对 tick、速度反馈、速度误差、电流命令和反馈电流；调试变量可配置速度 PID 的 `Kp`、`Ki`、`Kd`、总输出上限和积分上限。MotorDebug 仍只调用 MotorControl 正式接口，普通固件不编译这些试验入口。

开环试验确认当前机械条件下 `±250` 为临界起动命令，重复性不足；`±300` 各重复 3 次均可靠起动。纯 P 及多组受限 PID 在 `±0.1 rad/s` 下仍出现静止、突然起动、反向制动和再次静止的低速极限环，因此 `0.1 rad/s` 未作为基础速度环的验收工作点。

最终速度 PID 使用 `Kp=1800`、`Ki=5`、`Kd=0`、`max_output=500`、`max_i_output=150`，在输出轴 `±0.5 rad/s`、控制周期 10 ms、单次 5 s 条件下完成正反方向各 3 次复验。正向末段 100 点平均速度为 `0.4812~0.4830 rad/s`，负向为 `-0.4778~-0.4839 rad/s`；末段平均绝对误差为 `0.0584~0.0668 rad/s`，目标 `±0.1 rad/s` 带内覆盖率为 `79%~85%`。六次共提交 3000 个控制帧，发送失败为 0，试验结束均回到松弛和零电流。直接施加 `±0.5 rad/s` 阶跃时峰值绝对速度仍为 `0.724~0.797 rad/s`，后续上层实际控制应使用目标斜坡减少阶跃冲击。

上述参数已写入 MotorControl 的正式默认速度 PID。它完成当前 `±0.5 rad/s` 基础速度环验证，不代表已经解决极低速稳速；若业务必须长期运行在 `±0.1 rad/s`，需要另行验证摩擦补偿或低速速度估计，不能仅继续增大 PID 增益。

## 2026-10-16：M2006 位置—速度双环真机整定

在保持已验收速度内环 `Kp=1800`、`Ki=5`、`Kd=0`、总输出上限 `500`、积分上限 `150` 不变的条件下，为 MotorControl 增加位置 PID 配置和位置目标正式接口。MotorDebug 增加相对位置试验，记录位置、位置误差、位置环输出的速度目标、速度反馈和电流命令；试验仍经 MotorControl 调用 M2006 双环，不在调试模块或线程中直接运行 PID。

外环先比较纯 P：`Kp=1.0` 响应单调但收敛偏慢；`Kp=2.5` 在 5 s 末进入 `±0.02 rad`，仍有静摩擦残差；`Kp=5.0` 精度提高，但速度峰值达到约 `0.82 rad/s`。将外环速度上限由 `0.5 rad/s` 降至 `0.3 rad/s`，选取 `Kp=3.5` 后，`±0.2 rad` 的纯 P 最终误差分别约为 `+0.0133 rad` 和 `-0.0076 rad`。加入小积分 `Ki=0.002`、积分输出上限 `0.05 rad/s` 后，正负最终误差均降至约 `0.004 rad`，没有位置越过目标或持续振荡。

最终位置 PID 为 `Kp=3.5`、`Ki=0.002`、`Kd=0`、速度输出上限 `0.3 rad/s`、积分输出上限 `0.05 rad/s`。候选参数下，`±0.2 rad` 重复试验约在 `1.04~1.15 s` 后持续进入 `±0.02 rad` 误差带，`±0.1 rad` 最终误差绝对值约为 `0.0057~0.0060 rad`；各组最后 1 s 的样本均 100% 位于 `±0.02 rad` 内。使用固件默认参数重新烧录后的最终 `±0.2 rad` 试验各记录 500 点，CAN 发送失败均为 0，最终误差分别为 `-0.0054 rad` 和 `+0.0053 rad`，两方向各越过目标一次，最大位置超调约 `0.0056 rad`，没有持续振荡。速度内环对阶跃目标仍会产生瞬时速度峰值，最终两组约为 `0.63 rad/s`，因此业务层的大幅位置跳变仍应采用目标规划或轨迹斜坡。
