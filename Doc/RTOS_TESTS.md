# RTOS 板端自动化测试

当前分支：codex/rtos-serial-tests。

本套测试在 STM32F405 上运行真实内核，通过 USART1（PB6 TX、PB7 RX，115200、8N1）输出结果。电脑端读取 COM3、发送 RUN，核对用例顺序、数量和汇总，并保存原始日志及 JSON 报告。每次复位运行一轮；结果为 FAIL 时脚本返回非零退出码。

## 一键编译、烧录、测试

在项目根目录运行：

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run_rtos_tests.ps1 -Port COM3
~~~

脚本使用 TestDebug 预设，通过项目配置的 J-Link 烧录，然后打开串口测试。J-Link 使用 ExitOnError，烧录失败会停止流程。电脑已经具备 pyserial 3.5；换一台电脑时可执行：

~~~powershell
python -m pip install -r tests/requirements.txt
~~~

优化构建：

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run_rtos_tests.ps1 -Port COM3 -Preset TestRelease
~~~

若已手动烧录并复位，可只读取串口：

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run_rtos_tests.ps1 -Port COM3 -CaptureOnly
~~~

CaptureOnly 不会复位。如果上一轮已结束，应先按板子的复位按钮。串口需要由测试脚本独占。

正常 Debug/Release 预设默认关闭 RTOS_SELF_TEST，保留原来的信号量演示。测试预设拥有独立构建目录；flash_jlink 使用对应目录内生成的 Commander 脚本，避免烧错 Debug 的 ELF。

## 用例与预期

用例清单在 tests/firmware/rtos_test_cases.def，板端及判定器共用它。任务优先级为控制任务 1、中优先级工作任务 8、高优先级工作任务 31，以及内核自建的空闲任务 0。

| 用例 | 检查内容 |
| --- | --- |
| boot_priority | 启动顺序为 31、8、1，覆盖优先级位图最高位 |
| delay_lower_runs | 高优先级任务延时 20 tick 后恢复，延时期间低优先级任务可运行 |
| systick_preempts_busy | 低优先级任务不主动让出 CPU，高优先级任务仍由 SysTick 唤醒并抢占 |
| delay_zero | TaskDelay(0) 不登记阻塞或延时状态 |
| nested_critical | 嵌套临界区在内层退出后保持 BASEPRI，外层退出恢复原值 |
| semaphore_count | 连续释放及获取，计数准确，耗尽后立即失败 |
| semaphore_poll | 0 tick 获取空信号量不阻塞 |
| semaphore_timeout | 空信号量等待 20 tick 后返回失败，并清理等待状态 |
| semaphore_wake_preempts | 释放信号量后高优先级任务先运行，低优先级释放者才继续 |
| semaphore_priority | 多个等待者按 31、8 的优先级顺序唤醒 |
| semaphore_delete_busy | 有等待任务时删除失败，对象保持可用 |
| semaphore_timeout_cleanup | 超时后再释放可以正常获取，不残留等待位 |
| queue_fifo_wrap | 48 条消息分组入队出队，检查 FIFO、环形指针回绕及消息拷贝 |
| queue_poll | 满队列发送、空队列接收，0 tick 立即失败，接收缓冲区保持原值 |
| queue_send_timeout | 满队列等待发送 20 tick 后失败，原消息及等待状态正确 |
| queue_receive_timeout | 空队列等待接收 20 tick 后失败，缓冲区和等待状态正确 |
| queue_receive_wake | 发送者唤醒高优先级接收者，检查抢占及完整消息 |
| queue_send_wake | 接收者释放空位后唤醒高优先级发送者，FIFO 保持正确 |
| queue_delete_busy | 队列有等待者时删除失败，并仍可正常通信 |
| semaphore_stress | 128 次跨任务信号量握手，核对每轮共享数据与计数 |
| queue_stress | 128 次跨任务消息传递，核对序号、校验字段与等待位 |
| delay_tick_wrap | 将 tickBase 设置到 UINT32_MAX-10，检查跨回绕任务延时 |
| semaphore_tick_wrap | 同样设置 tick，检查跨回绕信号量超时 |
| queue_send_contention | 发送者被唤醒后空位又被高优先级任务占用，应继续等待并超时 |
| queue_receive_contention | 接收者被唤醒后消息又被高优先级任务取走，应继续等待并超时 |

时序用例记录内核 tick。20 tick 的正常延时/超时接受 20～25 tick，留出调度延迟；忙循环另外用 HAL 的 TIM1 时基设置 200 ms 上限。工作任务在测试期间不打印，只记录共享结果；控制任务在用例前后输出日志。

回绕用例是内部状态测试，会在临界区修改并恢复 tickBase，不是公开 API 的日常使用方式。两个队列竞争用例使用各自独立的容量 1 队列，并放在最后，避免错误队列污染前面的测试。所有对象及任务栈在启动调度前分配，测试不触发运行期间的堆并发分配。

本轮没有覆盖任务动态创建失败、同优先级轮转、ISR 通信、互斥锁/优先级继承、浮点上下文和栈溢出；通过这些用例不代表内核已经覆盖所有使用场景。

## 串口协议及报告

~~~text
RTOS_TEST READY version=1 total=25
RTOS_TEST START version=1 total=25
RTOS_TEST BEGIN id=delay_lower_runs
RTOS_TEST CASE id=delay_lower_runs status=PASS actual=20 expected=20
RTOS_TEST SUMMARY status=FAIL passed=23 failed=2 total=25
~~~

READY 每秒打印一次，脚本收到它后逐字发送 RUN。actual/expected 是用例选择的诊断值；板端 status 综合多个条件判断，不能只比较这两个数字。

判定器要求有 START、按清单完整出现 BEGIN/CASE、没有重复或中途复位、汇总计数一致。超时、缺项、乱码导致的丢项、串口错误和 ABORT 均不会判为通过。build/test-results 中保存每轮 UART 原文、JSON，以及一键脚本的配置/构建/烧录日志；JSON 的 host_firmware_sha256 是电脑上的 ELF 文件哈希，不是板端自报的镜像身份。

离线重放日志：

~~~powershell
python scripts/rtos_test_runner.py --log tests/results/baseline-TestDebug.log
python -m unittest discover -s tests -p 'test_*.py' -v
~~~

判定器自检使用人工生成的日志，不能当作硬件测试结果。

## 2026-10-04 实测基线

在原有内核实现上分别编译、J-Link 烧录并通过 COM3 实测：

| 构建 | 编译/烧录 | 板端结果 | FLASH | RAM |
| --- | --- | --- | --- | --- |
| TestDebug，-O0 | 成功 | 23 PASS、2 FAIL | 32,892 B | 11,432 B |
| TestRelease，-Os | 成功 | 23 PASS、2 FAIL | 20,260 B | 11,432 B |

原始串口证据及 JSON 位于 tests/results/baseline-TestDebug.* 和 baseline-TestRelease.*。两轮均没有协议错误或缺失用例。判定器 11 项自检通过，原有 Debug 演示仍可编译。

失败 1：queue_send_contention。容量为 1 的队列被写成 messageNumber=2，预期为 1。中优先级发送者等待满队列，高优先级任务取走消息、唤醒发送者，然后立即重新填满队列。发送者恢复执行后直接写入，没有复查队列是否仍有空位。

失败 2：queue_receive_contention。空队列 messageNumber 减成 4294967295，预期为 0。高优先级任务发送一条消息、唤醒接收者，又立即取走该消息。接收者恢复后直接读取并递减计数，没有复查消息是否仍存在。

对应实现位于 User/my_rtos/src/queue.c 的 QueueSend/QueueReceive 恢复执行路径。后续修复应在临界区循环重新检查条件，并维护原始超时期限，避免每次重新阻塞都重新开始完整超时。本分支保留内核 .c 实现，便于先观察并复现缺陷。
