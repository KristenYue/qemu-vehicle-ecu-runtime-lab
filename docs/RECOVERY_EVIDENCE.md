# Linux 进程健康与恢复证据 v1

## 环境

- GitHub Actions `ubuntu-24.04` x86_64 Runner；
- 提交：`35698bda31e54a15a7fd65b2c33056d26273bdba`；
- 工作流 Run：[`44871197371`](https://github.com/KristenYue/qemu-vehicle-ecu-runtime-lab/actions/runs/44871197371)；
- 原始报告：[`benchmarks/linux-recovery-evidence-v1.json`](../benchmarks/linux-recovery-evidence-v1.json)；
- 报告 SHA-256：`2629b45616335acd90bc2f3e58b89af87dde21fadb4e802d6693723f625fbe98`。

Linux 与 Windows CI 均通过。Linux Job 完成真实 GCC 构建、CTest 和三类独立进程恢复场景，报告由工作流产出并作为 Artifact 保存后冻结进仓库。

## 结果

| 场景 | 验收结果 | 关键证据 |
|---|---:|---|
| 依赖 READY 顺序 | 通过 | `health-service` 于单调时钟 25728 ms 进入 READY，`vehicle-client` 随后启动 |
| 首次 crash 恢复 | 通过 | 首进程退出码 42；安排 100 ms 退避；从退出到新进程 READY 为 200 ms |
| 首次 hang 恢复 | 通过 | 心跳超时后标记 `UNHEALTHY`，SIGKILL 为 signal 9；从异常判定到新进程 READY 为 202 ms |

## 测量边界

- 200/202 ms 是 GitHub Actions x86_64 Linux 虚拟机上的单次进程恢复实验，不是硬实时保证，也不是统计分位数；
- crash 计时起点是 Execution Manager 收到并回收异常退出；hang 计时起点是周期检查将应用判定为 `UNHEALTHY`；
- 配置的首次退避为 100 ms，事件循环以 100 ms `timerfd` 周期检查，因此观测恢复时间包含最多一个调度检查周期；
- 应用 READY 表示完成项目 UDS 握手，不等价于深度业务健康或车辆功能可用；
- 本轮尚未运行在 QEMU ARM64/Buildroot，也未集成第一项目的车辆服务，不能称为 vECU 端到端结果。

## 能证明什么

这组实验能够证明 Execution Manager 在真实 Linux 上完成：

1. 依赖应用的 READY 门控；
2. 子进程异常退出识别与退出码保留；
3. 心跳停止检测、异常终止与回收；
4. 有预算的指数退避和进程重新进入 READY；
5. 事件顺序和恢复耗时的可复现记录。

它不能证明跨主机高可用、硬实时调度、AUTOSAR Execution Management 一致性、突然断电恢复或真实 ECU 性能。
