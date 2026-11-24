# QEMU ARM64 vECU 恢复证据 v1

## 环境与可复现身份

- GitHub Actions `ubuntu-24.04` Runner；
- 提交：`afbacd60a3657c391c936754a26dda8e57da9260`；
- 工作流 Run：[`45432728908`](https://github.com/KristenYue/qemu-vehicle-ecu-runtime-lab/actions/runs/45432728908)；
- QEMU：`qemu-system-aarch64` TCG，`virt` machine，Cortex-A53，2 vCPU，512 MiB；
- Buildroot：`2026.08.2`，guest Linux：`6.18.7`，架构标记：`aarch64`；
- 原始报告：[`benchmarks/qemu-arm64-recovery-evidence-v1.json`](../benchmarks/qemu-arm64-recovery-evidence-v1.json)；
- 报告 SHA-256：`704fdb04fa8dce2354f3c125fa7a2f3d8d358bc5c40a02c354af845aada04908`。

报告同时冻结以下构建产物摘要：

| 产物 | SHA-256 |
|---|---|
| Linux `Image` | `0a528b1bab19527b501c833e907b66c6226a67eedbcf2ffbc620acb35448bf8d` |
| `rootfs.ext2` | `bbe3f7601d98d6f2198eb62ce62ad820b197866300fb0e5163b837f14674abeb` |
| `vecu_execution_manager` | `4675300923ea198c26f8589abf0a289497208b1b583a6d2e6c253b2c44e0c8d7` |
| `vecu_faultable_service` | `cff6c019ccc44e1c8c0f57812ad2950749a9ce2b0be71634e1c284d61823fca9` |

## 结果

Buildroot guest 启动后，Execution Manager 拉起故障服务 PID 114；服务进入 READY 后按测试模式停止心跳。Manager 在心跳超时后将其标记为 `UNHEALTHY`，发送 SIGKILL、记录退出原因并按 100 ms 初始退避安排重启。新进程 PID 115 再次进入 READY，随后以退出码 0 正常结束，Manager 输出 `VECU_MANAGER_EXIT=0` 与 `VECU_EVIDENCE_COMPLETE`。

| 指标 | 单次结果 |
|---|---:|
| 首次 READY | 17040 ms |
| 判定 `UNHEALTHY` | 17406 ms |
| 新进程启动 | 17608 ms |
| 恢复 READY | 17611 ms |
| `UNHEALTHY` 到恢复 READY | 205 ms |

上一轮同一链路得到 204 ms，本轮为 205 ms。该接近性只说明两次 CI 事件序列一致，不能当作统计稳定性或时延保证。

## 能证明什么

这组证据证明静态 AArch64 二进制被打入固定 Buildroot rootfs，并在 QEMU ARM64 Linux 中实际完成启动、UDS 心跳、超时检测、异常终止、受控重启、重新 READY 和干净退出。报告由串口输出生成，缺少架构、摘要、完成标记或关键事件都会使 CI 失败。

## 测量边界

- 205 ms 是 GitHub Actions x86_64 host 上 QEMU TCG 的单次结果，不代表真实 Cortex-A53 ECU 性能，也不是硬实时保证；
- READY 只表示项目心跳协议握手完成，不等于车辆业务功能完成深度健康检查；
- 本场景验证单应用 hang 恢复，不覆盖断电恢复、跨 ECU 协调、AUTOSAR 一致性或重启预算持久化；
- x86_64 Linux 的 200/202 ms 与本次 205 ms 只能比较事件语义，不能用于论证架构性能差异。
