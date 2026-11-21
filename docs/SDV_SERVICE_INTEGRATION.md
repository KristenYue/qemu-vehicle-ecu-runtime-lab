# 真实 SDV 车辆服务管理与恢复证据 v1

## 链路

```text
Execution Manager
  -> Managed Process Adapter
    -> sdv-communication-integration-lab/sdv_someip_vehicle_server
      -> vehicle.state/GetSnapshot
```

Managed Process Adapter 没有复制第一项目的车辆网关、服务运行时或 SOME/IP 代码。CI 独立检出第一项目，构建其原始 `sdv_someip_vehicle_server` 与 `sdv_someip_vehicle_client`；适配器只负责子进程启动、READY 输出探测、UDS 心跳代理和父进程死亡时的服务树回收。

## 环境与来源

- GitHub Actions `ubuntu-24.04` x86_64 Runner；
- 第二项目提交：`e5608510346c2a3f3de4639505aa0a36e384ce0c`；
- 工作流 Run：[`44871795080`](https://github.com/KristenYue/qemu-vehicle-ecu-runtime-lab/actions/runs/44871795080)；
- 受管服务仓库：[`KristenYue/sdv-communication-integration-lab`](https://github.com/KristenYue/sdv-communication-integration-lab)；
- 原始报告：[`benchmarks/linux-sdv-integration-evidence-v1.json`](../benchmarks/linux-sdv-integration-evidence-v1.json)；
- 报告 SHA-256：`302991853eeb0be66ee8c541a26343e9a2be3f36d0cc3c0dd4e483c8d460f9cd`。

## 验收结果

1. 第一服务树进入 READY，客户端经 localhost UDP 调用有界 SOME/IP `vehicle.state/GetSnapshot`；
2. 响应为 123.45 km/h、75% 电量、车门关闭、四组信号新鲜；
3. 对受管适配器注入 SIGKILL；其车辆服务子进程通过 `PR_SET_PDEATHSIG` 一并退出，避免遗留孤儿服务占用端口；
4. Execution Manager 记录 signal 9，执行 100 ms 退避并启动第二服务树；
5. 从注入 SIGKILL 到第二服务树 READY 实测 199 ms；
6. 恢复后连续两次车辆快照调用成功，字段与首次调用完全一致；
7. 第二服务端完成两次请求后正常退出，Execution Manager 不再重启并干净结束。

## 边界

- 199 ms 是一次 GitHub Actions x86_64 Linux 实验，不是统计分位数、硬实时保证或服务等级目标；
- SIGKILL 注入目标是适配器进程；子车辆服务的退出由 Linux parent-death signal 保证，不代表突然断电；
- SOME/IP 链路是第一项目定义的 localhost UDP 有界子集，不是量产 AUTOSAR SOME/IP 栈或物理车载以太网；
- 当前仍未运行于 QEMU ARM64/Buildroot，因此只能称为 Linux 系统集成证据，不能称为 ARM64 vECU 结果。
