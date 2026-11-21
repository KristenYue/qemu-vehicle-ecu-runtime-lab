# QEMU Vehicle ECU Runtime Lab

面向上海车载基础软件、vECU、系统集成与研发工具链岗位的 C++/Linux 系统软件项目。

项目主语是：**在 ARM64 嵌入式 Linux/vECU 环境中研发 Execution Manager，管理车载应用的依赖启动、进程生命周期、健康状态、故障恢复与系统证据。** QEMU 和 Buildroot 是可复现运行环境，不是项目唯一成果。

## 当前完成：P0 第一纵切

- 定义版本化应用 manifest，包含命令、依赖、启动超时和受控重启策略；
- 实现严格解析和校验：拒绝重复应用、未知字段、非法数值、缺失命令和未知依赖；
- 实现稳定拓扑排序和依赖环检测；
- 实现 Linux Execution Manager：`posix_spawnp`、`signalfd(SIGCHLD)`、`epoll`、`timerfd`、子进程回收、退出原因记录、指数退避和重启预算；
- 实现有界 UDS 心跳协议、应用身份/PID 校验、`STARTING/READY/UNHEALTHY` 状态、启动超时与卡死检测；依赖应用只有在上游 READY 后才启动；
- 提供两个受管示例进程，验证依赖顺序、异常退出和恢复；
- 提供正常依赖启动、首次 crash 恢复、首次 hang 恢复三类 Linux 自动化证据场景，并记录故障到重新 READY 的时序；
- 使用通用 Managed Process Adapter 管理第一项目的真实 SOME/IP 车辆状态服务；适配器负责 READY 探测、心跳代理和父进程死亡时的服务树回收，不复制车辆服务实现；
- 固定 Buildroot 2026.08.2，交叉编译静态 AArch64 Execution Manager 与故障服务，并在 QEMU `virt`/Cortex-A53 上自动执行启动、心跳超时、重启和关机证据链；
- 核心 manifest/依赖图逻辑支持 Windows 与 Linux 构建，Linux 负责真实进程生命周期集成。

当前还没有完成 QEMU/ARM64、Buildroot、cgroups 或第一项目车辆服务接入。README 只陈述已经落地的能力，不把后续规划写成完成项。

## 运行模型

```text
apps.manifest
      |
      v
严格解析与依赖图校验
      |
      v
C++ Execution Manager
  epoll + timerfd + signalfd
      |
      +--> health-service
      |
      +--> vehicle-client (depends on health-service)
```

## Manifest

```ini
[health-service]
command=./vecu_health_service
depends=
restart=on-failure
max_restarts=3
restart_backoff_ms=100
start_timeout_ms=2000
heartbeat_timeout_ms=500

[vehicle-client]
command=./vecu_vehicle_client
depends=health-service
restart=on-failure
max_restarts=2
restart_backoff_ms=100
start_timeout_ms=2000
heartbeat_timeout_ms=500
```

命令使用空格分词，当前不实现 shell 引号和重定向，避免隐式调用 shell。应用名和依赖必须在同一文件内唯一且可解析。

## 构建

Linux：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
cd build
./vecu_execution_manager ../config/apps.manifest
```

Windows 当前只构建可移植核心与测试：

```powershell
.\scripts\build_windows.ps1
```

## 工程边界

- 这是面向 vECU 的系统软件实验室，不是量产 ECU 或 AUTOSAR Execution Management 实现；
- READY 仅表示应用完成项目心跳协议握手，不等于所有业务依赖已经通过深度健康检查；
- 当前重启预算仅保存在单次 Execution Manager 进程内，不声称跨重启持久化；
- QEMU 软件仿真数据不会包装成真实 ARM64 硬件性能。

## Linux 故障恢复证据

```bash
python3 scripts/run_linux_recovery_evidence.py \
  --manager "$PWD/build/vecu_execution_manager" \
  --config-dir "$PWD/config" \
  --output "$PWD/artifacts/linux-recovery-evidence.json"
```

脚本验证三个独立进程级场景：依赖服务必须在上游 READY 后启动；服务首次异常退出后按预算恢复；服务首次停止心跳后被判定为 `UNHEALTHY`、终止并恢复。报告保留事件顺序和单调时钟差值。只有真实 Linux 执行成功后，报告数字才能进入项目结论。

GitHub Actions `ubuntu-24.04` 首轮实测三类场景全部通过：crash 进程以退出码 42 终止，从回收到新进程 READY 为 200 ms；hang 进程因心跳超时被标记为 `UNHEALTHY` 并收到 SIGKILL，从异常判定到新进程 READY 为 202 ms。它们是单次 x86_64 CI 结果，不是统计分位数或硬实时保证。原始结果与边界见[恢复证据说明](docs/RECOVERY_EVIDENCE.md)。

跨仓库 CI 进一步构建并管理第一项目的真实 SOME/IP 车辆状态服务：首次调用成功后杀死受管适配器及其服务树，Execution Manager 在 199 ms 后恢复 READY；恢复后两次 `vehicle.state/GetSnapshot` 均返回与首次调用一致的 123.45 km/h、75% 电量及四组信号新鲜状态。方法、原始结果和边界见[真实 SDV 服务集成说明](docs/SDV_SERVICE_INTEGRATION.md)。

## 下一验收点

1. 结构化 JSONL 事件日志与 crash/hang 故障报告；
2. 冻结 QEMU ARM64 原始串口报告并记录镜像、内核和二进制摘要；
3. x86_64 与 QEMU ARM64 双环境故障恢复对照；
4. cgroups v2 资源上限与观测。

