# 21 — 独立小协议抽查 + 生成层销毁语义备忘（自主审计，基线 e38076ac9）

## 备忘：hyprwayland 生成包装器的销毁双通道语义（供全部 UAF 类发现引用）

`protocols/fractional-scale-v1.cpp:134-144`（生成代码，各协议同构）：
- destroy **请求** dispatcher 只调用 `requests.destroy`（`setDestroy` 注册的 handler），**不**调 `wl_resource_destroy`；
- `setOnDestroy` 注册的是 `wl_resource_add_destroy_listener`，仅在资源真正销毁（断连/包装器析构）时触发；
- 包装器析构（`:100-108`）**先** `wl_list_remove` 摘除 destroy listener、清 user_data，**再** `wl_resource_destroy`。
结论：`setDestroy`+`setOnDestroy` 双注册在客户端显式 destroy 情形下**只发生一次**业务回调（请求 handler 里 erase 容器 → UP 析构包装器 → 其析构已摘除 listener，销毁监听不再触发）。据此，`setDestroy+setOnDestroy` 双注册本身**不构成** double-fire UAF；仅当 handler 捕获**对象外部持有的裸指针**（不随容器 erase 一起销毁）时才可能有二次解引用。此结论用于修正/否决潜在的 double-destroy 类报告（与 F-INP-08 的 `partially` 判定一致）。

## F-FS-01（信息级）FractionalScale：raw-PADDON 捕获双注册

- 位置：`src/protocols/FractionalScale.cpp:45-46`（`setDestroy`/`setOnDestroy` 均捕获 `removeAddon(PADDON)` 裸指针）。
- 分析：依赖上节语义，客户端显式 destroy 单次触发、断连路径单次触发，常规不可达二次解引用。**列为信息级**：该写法把正确性押在生成层"析构先摘 listener"的不变式上（对比同仓 `ExtWorkspace.cpp:26-27` 用 WP 的安全写法）。建议统一改为 WP 捕获或 `erase_if` 按 resource 指针比较，消除对生成层语义的隐式依赖。

## 抽查负面结果（已读、未见可利用问题）

- `src/protocols/FractionalScale.cpp` 其余：addon 唯一性检查（:27-34）、`m_surfaceScales`/`m_addons` 键均为 `WP<CWLSurfaceResource>`（`FractionalScale.hpp:59-60`，弱键无 surface 驻留泄漏——审计中一度怀疑强引用泄漏，经核为误报，予以记录以正视听）。
- `src/protocols/PointerWarp.cpp:27-79`：warp 前校验 `pointerFocus == PSURFACE` + `serialValid` + 1px 边界回验；`POINTER` 判空；`PSEAT` 允许为空（`serialValid` 对 null seat 返回 false，`SeatManager.cpp:135-136`）→ 均安全。`ASSERT(container)`（:140）在 seatResource 非空但不在 `m_seatResources` 时 release 下解引用——评审认为不可达（容器随 seat bind 建立），未立项，建议加 `if (!container) return false` 一行防御。
- `src/protocols/IdleInhibit.cpp`（52 行）、`src/protocols/SinglePixel 等微型协议`：无数字解析、无资源交叉引用，未见问题。
