# QEMU Vehicle ECU Runtime Lab

面向上海车载基础软件、vECU、系统集成与研发工具链岗位的 C++/Linux 系统软件项目。

项目主语是：**在 ARM64 嵌入式 Linux/vECU 环境中研发 Execution Manager，管理车载应用的依赖启动、进程生命周期、健康状态、故障恢复与系统证据。** QEMU 和 Buildroot 是可复现运行环境，不是项目唯一成果。

## 当前完成：P0 第一纵切

- 定义版本化应用 manifest，包含命令、依赖、启动超时和受控重启策略；
- 实现严格解析和校验：拒绝重复应用、未知字段、非法数值、缺失命令和未知依赖；
- 实现稳定拓扑排序和依赖环检测；
- 实现 Linux Execution Manager 初版：`posix_spawnp`、`signalfd(SIGCHLD)`、`epoll`、`timerfd`、子进程回收、退出原因记录、指数退避和重启预算；
- 提供两个受管示例进程，验证依赖顺序、异常退出和恢复；
- 核心 manifest/依赖图逻辑支持 Windows 与 Linux 构建，Linux 负责真实进程生命周期集成。

当前还没有完成 QEMU/ARM64、Buildroot、UDS 心跳、hang 检测、cgroups 或第一项目车辆服务接入。README 只陈述已经落地的能力，不把后续规划写成完成项。

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

[vehicle-client]
command=./vecu_vehicle_client
depends=health-service
restart=never
max_restarts=0
restart_backoff_ms=100
start_timeout_ms=2000
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
- 当前进程“运行”只表示成功创建且尚未退出，不等于应用完成业务就绪；P1 将使用 UDS 心跳协议区分 `STARTING/READY/UNHEALTHY`；
- 当前重启预算仅保存在单次 Execution Manager 进程内，不声称跨重启持久化；
- QEMU 软件仿真数据不会包装成真实 ARM64 硬件性能。

## 下一验收点

1. UDS 心跳、ready 协议和 hang 检测；
2. 结构化事件日志与 crash/hang 故障报告；
3. 将第一项目的一个只读车辆状态服务作为真实受管应用；
4. Buildroot ARM64 镜像与 QEMU `virt` 集成测试。

