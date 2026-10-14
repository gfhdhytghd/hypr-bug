# Hyprland HDR / 色彩管理 — 崩溃问题详报与全量核实结果

- 核实基线：分支 `cm/tonemap-ref-scaling`，工作树 `/home/wilf/data/Hyprland-cm-tonemap`，tip `30e438818`（"renderer: fix on HDR render bug"，基于 main `6cc0822e2`）
- 规范：`/usr/share/wayland-protocols/staging/color-management/color-management-v1.xml`（wp_color_manager_v1 v3）
- 说明：审计文档（hyprland-hdr-cm-audit.md）记载的 HEAD `b2586b63b` 在本地仓库不存在（分支疑似审计后被 rebase），本报告所有行号以当前 tip 重新复核
- 全程只读核实，未改动仓库任何文件

## 〇、全量核实结论总表（29 条）

| # | 原核实状态 | 本次裁决 | 备注（修正行号 / 修正内容） |
|---|---|---|---|
| 1 | 实测 | 属实 | 行号 460/901 准确；见下方报告一 |
| 2 | 实测 | 属实 | 行号 269-270/767/316-324 准确；见下方报告二 |
| 3 | 已核对 | 属实 | handler 起于 :303 |
| 4 | 已核对 | 属实 | 行号全部准确；单位无错，错在未按 PQ 规则强制 min+10000 |
| 5 | 未复核 | 属实 | 行号修正：hpp:414-431（默认值 :239-242）、Renderer.cpp:2511-2515 |
| 6 | 未复核 | 属实 | :550 处自带 FIXME "actually check completeness" |
| 7 | 未复核 | 属实 | 全仓默认协议版本即 1；附加发现：already_set 检查整段被注释（:710-736） |
| 8 | 未复核 | 属实 | :714 准确 |
| 9 | 未复核 | 属实 | :592-607 准确；v1 收 compound(14)、v≥2 收 srgb/ext_srgb 均不拒 |
| 10 | 未复核 | 属实 | :620 准确；0=incomplete_set，creator 的 unsupported_feature=2 |
| 11 | 未复核 | 属实 | 全文件从未 post SURFACE 的 inert 错误 |
| 12 | 未复核 | 属实 | 行号修正：Monitor.cpp:652-664；唯一通知点在 applyCMType :612-613 |
| 13 | 已核对 | 属实 | CPU 侧文件修正：src/helpers/cm/ColorManagement.cpp:222-225 + 300-302（非 protocols/）；V=0.05 时暗部亮 ~15× |
| 14 | 高（数值未复核） | 属实 | 全仓 grep -i ootf 零命中；75% 白手算 = 265.0 nit，与文档"约265"精确一致（正确加 BT.2100 OOTF γ=1.2 后应 ≈153 nit） |
| 15 | 已核对 | 属实 | 路径修正：src/render/shaders/glsl/tonemap.glsl（无 pass/ 子目录），行号 46/80 正确；FP16 缓冲色域 BT.709 证据链闭环（Monitor.cpp:2692-2710 → hpp:432-438） |
| 16 | 已核对 | 属实 | 表值与 KWin 修复提交 fad8a10a 修复前逐字节相同：H.273 按 G,B,R 序列表未换序；正确序 R(0.681,0.319) G(0.243,0.692) B(0.145,0.049)，白点本就对 |
| 17 | 未复核 | 属实（机制修正） | xy2xyz(0,0) 不除零（库与 GLSL 均有 y==0 防护）；NaN 实际产生于 toXYZ()/convertMatrix 的奇异矩阵求逆与白点归一；可达路径：protocols:644 set_primaries_named 不拦 CIE1931_XYZ → uniform 全 NaN |
| 18 | 未复核 | **部分属实** | max/reference 实际参与 tonemap（Renderer.cpp:2147-2149、:2168）；只有 min 完全不参与。行号修正：srcTFRange 在 2164-2165 |
| 19 | 源码已核对 | 属实 | HLG 直出 + 空元数据 + 反复切换代码通路确认（Renderer.cpp:2503 TODO、:2576-2587、:2599-2617） |
| 20 | 已核对 | 属实 | maxLuminance(int defaultValue=80) 实现在 Monitor.cpp:2364-2366；条件式恒真 |
| 21 | 已核对 | 属实 | 补充：mirror 纹理格式还硬编码 XRGB8888/XRGB2101010 定点（:140/:167），无 FP16/EDR；消费点 Renderer.cpp:2212 |
| 22 | 未复核 | 属实 | DS 资格只有 surfaceIsHDR 与 inHDR() 两布尔；CM_NS_IGNORE 时连 CTM 补偿也没有 |
| 23 | 未复核 | 属实 | 行号修正：Renderer.cpp:2510、:2534-2535；附加发现：maxCLL() 实现以 m_maxLuminance 优先 |
| 24 | 未复核 | **部分属实** | FP16 依赖仅在 work-buffer/mirror 捕获路径成立（tonemapShaderMaxLuminance linear→sRGB 630→248，TonemapPeak.cpp:44-49）；直接重渲 surface 的截图（ScreenshareFrame.cpp:414/456）两模式同走 PQ 分支，未见必然差异。行号：2147-2149 |
| 25 | 未复核 | 属实 | SDR uniform 仅在 work buffer TF==ST2084_PQ 时传真实值，FP16 下恒 EXT_LINEAR → 恒 1.0，无补偿点 |
| 26 | 未复核 | **部分属实** | "提交晚一帧"不严格：状态变更进入本次 commit，实为内容滞后一帧/模式与内容错配一帧；且退出后无新 damage 时损伤门（:2280-2283）可把切换无限期推迟。行号修正：:2702 |
| 27 | 前期实测 | 属实，修复正确 | tonemapShaderMaxLuminance（:2091-2104）消除单位失配；9 个单测实测全部通过。盲区：未覆盖 getCMSettings 的 needsTonemap/tonemapMode==2 三分支、linear 目标 headroom 边界（dstMax 略大于 203 一侧）、dstMax 缺省 10000、shader 端绝对 nit 不变量无集成测试 |
| 28 | — | 属实 | tip 仍无锚定；srcRefLuminance 形参在 tonemap 函数体内从未被引用（死参数），可一并清理 |
| 29 | — | 属实 | 三处行号准确（hpp:21、Monitor.cpp:582/596）；无任何 monitor rule 可配输出参考白；sdr_max_luminance 默认 80（MonitorRule.hpp:61），与 203 不联动 |

次要项抽查：a（预乘 alpha 后 tonemap）属实；b（max_cll/fall 报 0）**部分属实**——两者皆未设时根本不发，一侧设置时另一侧以 0 发出，实际发送处为 info 构造函数 ColorManagement.cpp:856-858（文档所称 sendInformationEvent 不存在）；c（ICC 走 parametric、sendIccFile 被注释 :814-816）属实；d（m_currentPreferredId 陈旧致重复发送）属实。

附加新发现（原审计未列）：
- `set_mastering_luminance` / `set_max_cll` / `set_max_fall` 的 already_set 检查整段被注释禁用（ColorManagement.cpp:710-736）
- `get_preferred_parametric` ICC 分支 FIXME 只打日志不报 unsupported（:446-450）
- `shouldChangePreferredImageDescription()` 恒 false（Compositor.cpp:1016-1019）+ `onImagePreferredChanged(0)` 硬编码 id 0 → preferred 变更通知机制半接线
- tonemap.glsl:36-42 ICtCpHLG 矩阵整段被注释，HLG 的 ICtCp 恒用 PQ 版

---

## 报告一：feedback 在 surface 销毁后未转 inert → onPreferredChanged 空指针段错误

### 1. 摘要与严重度

`CColorManagementFeedbackSurface` 持有 `WP<CWLSurfaceResource> m_surface`（src/protocols/ColorManagement.hpp:84）。规范要求其关联 wl_surface 销毁后对象变 inert，但实现中 feedback 从不因 surface 死亡被移除或标记 inert：它留在 `PROTO::colorManagement->m_feedbackSurfaces`，仅客户端显式 destroy 反馈资源才移除（src/protocols/ColorManagement.cpp:393-400 → destroyResource :921-923）。之后任何输出描述变更会广播到全部 feedback，`onPreferredChanged()` 在 :460 无 `expired()` 保护直接解引用 `m_surface`，hyprutils `WeakPtr.hpp:221-226` 对失效 WP 的 `get()`/`operator->` 返回 nullptr → SIGSEGV。

**严重度 Critical**：任意客户端一条请求序列（建 feedback → 销毁自己的 surface）即可在下次配置/输出变更时崩掉整个合成器（远程 DoS）。

### 2. 涉及对象的生命周期

- **创建**：CColorManager `setGetSurfaceFeedback` 处理器（ColorManagement.cpp:115-135；emplace 进 `m_feedbackSurfaces` :125-126，`m_self` 回填 :134）；ctor :386-457 存 WP `m_surface`（:387）并注册 enter/leave 监听（:455-456）。
- **持有**：协议单例 vector `m_feedbackSurfaces`（hpp:213）持 SP。
- **被引方 CWLSurfaceResource**：`wl_compositor.create_surface` 创建，由 `CWLCompositorProtocol::m_surfaces` 持 SP；客户端 `wl_surface.destroy` → core/Compositor.cpp:94-95 → `CWLSurfaceResource::destroy()`（core/Compositor.cpp:306-317）→ :315 destroyResource → :914-916 从 m_surfaces 摘除 → 最后 SP 释放后对象死亡，feedback 的 WP 变 expired。**feedback 本身无任何 surface-death 处理。**
- **旁证**：`get_preferred`(:405) / `get_preferred_parametric`(:428) 均有 `if (m_surface.expired())` → ERROR_INERT（:406/:429），证明过期是常态——唯独 `onPreferredChanged` 漏了同样防护。

### 3. 崩溃触发时序与调用链

客户端：bind wp_color_manager_v1（render:cm_enabled=1，ProtocolManager.cpp:234-235）→ create_surface S（不 map，保证无其他 SP 持有）→ `get_surface_feedback(S)`=F → `wl_surface.destroy(S)` → roundtrip。此后 F 无人再触碰。

合成器端：任何改变监视器图像描述的操作（如 `hyprctl keyword monitor eDP-1,max_luminance 600`）→ `CMonitor::applyMonitorRuleSoft` → `applyCMType`（Monitor.cpp:648）→ :604-609 用 minLuminance()/maxLuminance() 覆盖 → :610 `oldImageDescription != m_imageDescription` 成立 → :612-613 `PROTO::colorManagement->onMonitorImageDescriptionChanged(m_self)`。

调用链：

```
onMonitorImageDescriptionChanged (ColorManagement.cpp:895)
  → :896-899 outputs 发 image_description_changed（正常）
  → :901-902 无守卫遍历全部 feedback
  → feedback->onPreferredChanged() (:902)
  → onPreferredChanged (:459)
  → :460 m_surface->m_enteredOutputs.size()
  → WP 已 expired，operator-> 返回 nullptr → 空 this 读成员 → SIGSEGV
```

说明：surface 死亡后 enter/leave 监听器随信号一起消亡，所以 :901 的广播是唯一但必然的触发入口。

### 4. 崩溃点代码

ColorManagement.cpp:459-461：

```cpp
void CColorManagementFeedbackSurface::onPreferredChanged() {
    if (m_surface->m_enteredOutputs.size() == 1) {
        const auto newId = m_surface->getPreferredImageDescription()->id();
```

失效指针：`CColorManagementFeedbackSurface::m_surface`，类型 `WP<CWLSurfaceResource>`（protocols/ColorManagement.hpp:84）。`WeakPtr.hpp:221-226`：`get() = (impl_ && impl_->dataNonNull()) ? m_data : nullptr`；`operator->` 直接返回它。

### 5. 规范差距

color-management-v1.xml L795-799：*"If the wl_surface associated with this object is destroyed, the wp_color_management_surface_feedback_v1 object becomes inert."*；L826-827：*"If this protocol object is inert, the protocol error inert is raised."*（错误枚举 inert=0，L809-810）。实现无对象级 inert 转换，仅两个请求有惰性 expired() 检查；内部广播路径完全无防护并崩溃。

### 6. 修复建议与复现

- **最小修复**：`onPreferredChanged` 入口（:459 函数体第一行）加 `if (m_surface.expired()) return;`
- **推荐修复（规范语义）**：ctor（:455-456 旁）追加监听 `m_surface->m_events.destroy`（`CWLSurfaceResource::destroy()` 在 core/Compositor.cpp:313 emit），回调中 `PROTO::colorManagement->destroyResource(this)`，使 surface 死亡即摘除 feedback；同时修掉 :890-892 `onImagePreferredChanged` 向应已 inert 对象发事件的次要问题；保留 :405/:428 检查。
- **复现**（约 15 行 wayland-client C 程序）：
  1. bind wp_color_manager_v1
  2. `create_surface` S；`get_surface_feedback(S)`=F；roundtrip
  3. `wl_surface_destroy(S)`；roundtrip（F 保留）
  4. 终端执行 `hyprctl keyword monitor <monitor>,max_luminance 600`
  5. hyprland 主线程 SIGSEGV，backtrace `onMonitorImageDescriptionChanged → onPreferredChanged`，fault address ≈ 0x0 + offsetof(m_enteredOutputs)

---

## 报告二：failed（未 ready）的 wp_image_description_v1 仍可 get_information / 被 set_image_description 使用 → 空 m_settings 解引用

### 1. 摘要与严重度

`CColorManagementOutput::get_image_description` 在输出失效时正确发 failed(NO_OUTPUT)（ColorManagement.cpp:269-270），但该 failed 对象：

- (a) 在判定成败之前就已被 emplace 进 `m_imageDescriptions`（:259-260），失败后不摘除；
- (b) `m_settings` 保持默认构造空 WP（赋值仅在 else 分支 :272）；
- (c) 创建时 `allowGetInformation=true`（:260 传入 true）；
- (d) `CColorManagementImageDescription` 无任何 ready/failed 状态位，`WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY` 全文件从不 post。

后果：failed 后 `get_information` → :767 `m_settings->value()` 空指针崩溃（主线程）；或 failed 对象被 `set_image_description` 挂上 surface → :323-324 拷贝空 WP 且 `hasImageDescription=true` → 渲染循环 `isHDR()`/`imageDescription()`（:378/:347，经 Renderer.cpp:2571/2581、Monitor.cpp:2016-2017/2402）空指针崩溃（渲染线程，整机挂死）。

**严重度 Critical**：恶意/出错客户端单连接即可稳定崩合成器。

### 2. 涉及对象的生命周期

- **创建**：`CColorManagementOutput` ctor 的 setGetImageDescription lambda（:253-276）：:256-257 先销毁旧描述；:259-260 emplace 进 `CColorManagementProtocol::m_imageDescriptions`（protocols/ColorManagement.hpp:216 持 SP，构造第二参 true=allowGetInformation）；:268 `m_self` 回填；:269-270 failed 分支仅 sendFailed。
- **m_settings**：`NColorManagement::PImageDescription`（protocols/ColorManagement.hpp:166），`using PImageDescription = WP<const CImageDescription>`（src/helpers/cm/ColorManagement.hpp:383）；默认构造 = expired。
- **销毁**：仅 destroy/onDestroy（:757-758 → destroyResource :933-935）；sendFailed 不摘除、不清理。
- **消费者**：get_information lambda（:760-774，只查 :762 `m_allowGetInformation`）；set_image_description 的 find_if（:316-317，只按 `wl_resource*` 身份匹配，不查 ready）；渲染/输出侧 Renderer.cpp:2570-2581、Monitor.cpp:2016-2017、2402。
- **同源污染**：`CColorManager::get_image_description` 引用复制失败路径（:219-223 sendFailed(UNSUPPORTED)）同样留下空 `m_settings` 对象，仍可被 :316-317 命中用于 set_image_description（其 allowGetInformation=false 挡住 get_information，但 set 路径不设防）。

### 3. 崩溃触发时序与调用链

**路径 A（get_information，崩溃最直接，无需任何 surface）**：

1. bind wp_color_manager_v1 + wl_output → get_output → cmout（`m_output` 为 `WP<CWLOutputResource>`，hpp:41）
2. 合成器端移除输出：`hyprctl disable <id>` / 拔线 / mirror 切换（ProtocolManager.cpp:120-123 retireOutputGlobal）→ `m_output` expired 或其 `m_monitor` WP 失效
3. `get_image_description()` → :259-260 RESOURCE 入列（allowGetInformation=true）→ :269 条件真 → :270 `sendFailed(CAUSE_NO_OUTPUT)`。规范：此刻该对象只能销毁
4. `get_information(id)` → :760 lambda → :762 放行 → :767 `m_settings->value()`：空 WP `operator->` 返回 nullptr → 以 this=nullptr 调 `CImageDescription::value()` → SIGSEGV

```
wl_event_loop dispatch(get_information)
  → setGetInformation lambda (ColorManagement.cpp:760)
  → :762 m_allowGetInformation 放行
  → :767 m_settings->value() → null this → SIGSEGV
```

**路径 B（set_image_description，崩溃延后到渲染循环）**：

1. 同路径 A 第 1-3 步得 failed 对象 D
2. create_surface S → `get_surface(S)`=cmsurf → `cmsurf.set_image_description(D, perceptual)`：:306-310 user_data 有效放行；:311-314 intent 合法放行；:316-317 find_if 命中 D（failed 未清理）；:323 `setHasImageDescription(true)`；:324 `m_imageDescription = D 的空 m_settings`
3. attach+commit，窗口全屏后下一帧：Renderer.cpp:2565 FULLSCREEN_WINDOW 分支 → :2567 findWithCM → :2570 `hasImageDescription()==true` → :2571 `isHDR()` → ColorManagement.cpp:377-378 `m_imageDescription->value().transferFunction` → 空 WP 解引用 → SIGSEGV（渲染线程）
4. 不进全屏时 Monitor.cpp:2016-2017（isHDR/isWindowsScRGB）与 Monitor.cpp:2402（workBufferImageDescription→imageDescription→:347）同样裸解引用

为何走到：服务端把 ready/failed 状态完全寄托于事件发送、自身不记录；failed 后对象身份、列表成员资格、请求派发一切照旧。

### 4. 崩溃点代码

路径 A，ColorManagement.cpp:760-767（节选）：

```cpp
m_resource->setGetInformation([this](CWpImageDescriptionV1* r, uint32_t id) {
    if (!m_allowGetInformation) { r->error(WP_IMAGE_DESCRIPTION_V1_ERROR_NO_INFORMATION, ...); return; }
    auto RESOURCE = makeShared<CColorManagementImageDescriptionInfo>(
        makeShared<CWpImageDescriptionInfoV1>(r->client(), r->version(), id),
        m_settings->value());   // :767 — failed 分支从未给 m_settings 赋值
```

路径 B，:323-324 引入空值：

```cpp
setHasImageDescription(true);
m_imageDescription = imageDescription->get()->m_settings;
```

崩溃于 :377-378 `isHDR()`（`m_imageDescription->value().transferFunction ...`）或 :341-347 `imageDescription()`。

### 5. 规范差距

- XML L1412-1418：*"An object which is not ready is illegal to use, it can only be destroyed. Any other request in this interface shall result in the 'not_ready' protocol error. Attempts to use an object which is not ready through other interfaces shall raise protocol errors defined there."*
- L1435-1441：错误枚举 not_ready=0 / no_information=1（not_ready 从不 post）
- L745-747（set_image_description）：*"Image descriptions which are not ready … are forbidden in this request, and in such case the protocol error image_description is raised."*
- L1460-1474（failed 事件）：*"Once this event has been sent, the wp_image_description_v1 object will never become ready and it can only be destroyed."*
- :269-270 的 failed(no_output) 本身合规（L686-688），缺口仅在 failed 之后的防御。

### 6. 修复建议与复现

1. `CColorManagementImageDescription`（protocols/ColorManagement.hpp:154-175）加 `bool m_ready = false;`；在 `sendMaybeReady()` 成功分支（:794-808）置 true，所有 sendFailed 调用点（:221/:270/:511/:566/:803）置 false——建议封装 `markFailed(cause, msg)` 统一置位
2. `get_information`（:760）在 `m_allowGetInformation` 检查后加 `if (!m_ready) { r->error(WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY, "description is not ready"); return; }`
3. `set_image_description` 在 :316-321 命中后、:323 之前加 ready 检查，不 ready 则 `r->error(WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_IMAGE_DESCRIPTION, ...)`（对应 L745-747）；failed 条目可同时从 m_imageDescriptions 摘除
4. 纵深防御：`imageDescription()`/`isHDR()`/`isWindowsScRGB()` 或 Renderer.cpp:2570 处把 `!valid()` 视同无描述回落默认值，保证渲染线程永不因空 WP 崩
5. hyprtester 用例：disable monitor → get_image_description → failed → get_information 断言收到 not_ready 协议错误而非崩溃；set_image_description 断言 image_description 错误

**复现 A**（约 10 行 C，无 surface）：

1. `render:cm_enabled=1`；bind wp_color_manager_v1；registry 取 wl_output → get_output=cmout；roundtrip
2. 终端 `hyprctl disable <monitor id>`（或拔线）
3. `wp_color_management_output_v1_get_image_description(cmout)` → roundtrip 收到 `failed(cause=no_output)`
4. `wp_image_description_v1_get_information(desc, ...)` → roundtrip → hyprland SIGSEGV（ColorManagement.cpp:767，fault addr ≈ 0x0）

**复现 B**：第 3 步后改为 create_surface S → get_surface(S) → set_image_description(desc, 0=perceptual) → attach buffer + commit → 置全屏 → 下一帧 Renderer.cpp:2571 → ColorManagement.cpp:378 SIGSEGV。
