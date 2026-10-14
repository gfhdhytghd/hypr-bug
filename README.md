# hypr-bug — Hyprland wp_color_management v1 崩溃复现与审计

针对 Hyprland 分支 `cm/tonemap-ref-scaling`（tip `30e438818`，基于 main `6cc0822e2`）
的 color-management-v1 协议层审查产物：**两个 Critical 级 SIGSEGV 的可编译复现客户端**，
外加 29 项发现的完整核实报告。

## 目录

- `cm-crash-repro/` — 复现客户端（`build.sh` 一键编译出 `./repro`）
- `reports/hyprland-hdr-cm-crash-reports.md` — 中文：崩溃详报 + 全量核实总表
- `reports/hyprland-hdr-cm-crash-reports.en.md` — 英文版

## 两个必崩漏洞（普通客户端请求序列即可打挂合成器）

1. **feedback 未随 surface 死亡转 inert**
   客户端建 `wp_color_management_surface_feedback_v1` 后销毁自己的 `wl_surface`；
   下一次输出图像描述变化时，`onPreferredChanged`（`ColorManagement.cpp:460`）
   无守卫解引用过期 `WP<CWLSurfaceResource>` → 主线程 SIGSEGV。
   （规范：XML L795-799，surface 销毁后 feedback 必须 inert）
2. **failed（未 ready）的图像描述对象仍可使用**
   输出被移除后 `get_image_description` 收到 failed 的对象不从列表摘除、
   无 ready 状态位、`not_ready` 错误从不 post：
   - A：直接 `get_information` → `:767` 空 `m_settings` WP 解引用崩主线程
   - B：`set_image_description` 把它挂上窗口 → 渲染循环 `isHDR()`
     （`Renderer.cpp:2571` / `ColorManagement.cpp:378`）崩渲染线程

## 使用

⚠️ 会把目标 hyprland 连数据一起打挂，请只在嵌套/测试会话操作。
服务端需 `render:cm_enabled=1`（该分支默认开）。

```bash
cd cm-crash-repro && bash build.sh

# 崩溃 #1
./repro feedback
#   另一终端: hyprctl keyword monitor <输出名>,add,cm,hdr  → 回车

# 崩溃 #2A（主线程）
hyprctl output create headless VICTIM
./repro getinfo VICTIM
#   另一终端: hyprctl output destroy VICTIM → 回车

# 崩溃 #2B（渲染线程）
hyprctl output create headless VICTIM
./repro poison VICTIM
#   另一终端: hyprctl output destroy VICTIM → 回车
```

成功标准：hyprland 进程消失，崩溃栈含 `onPreferredChanged`（#1）或
`CColorManagementImageDescriptionInfo` / `isHDR`（#2）。
修复验证：补齐 expired()/ready 守卫后，以上应变成 inert / not_ready /
image_description 协议错误而非崩溃（详报各报告第 6 节）。

## 声明

发现与核实来自对规范的静态审查 + 嵌套会话实测（详见 reports/）。
修复未合并前请勿公开扩散；上报 Hyprland 请遵循其
[AI 使用政策](https://github.com/hyprwm/.github/blob/main/policies/AI_USAGE.md)
与 issue 指南，由本人直接与开发团队交互。
