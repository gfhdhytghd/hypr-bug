# Fork 安全审计：色彩管理 / HDR 子系统（相对上游 e2ad066c）

- 审计范围：`git diff e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb...HEAD`，聚焦 `src/protocols/ColorManagement.cpp`、`src/helpers/cm/*`、`src/output/Monitor.cpp`、`src/render/*`、GLSL shaders、`src/ipc/s1/Commands.cpp`。
- 基线说明：`audit-reports/hyprland-hdr-cm-crash-reports.md` 的 29 条旧发现中多数（如 #1 feedback 悬垂空指针崩溃、#4 PQ min+10000 规则、#17 NaN 矩阵）在本 fork 中已被修复（例如 `src/protocols/ColorManagement.cpp:356/381/413` 增加了 expired 守卫；`src/helpers/cm/ColorManagement.cpp:46-58` 增加了奇异矩阵拒绝）。以下为**新增/新激活**的漏洞，全部针对 fork 代码或其新打通的攻击路径。
- 攻击者模型：任意 Wayland 客户端（bind `wp_color_manager_v1`，`render:cm_enabled=1` 时默认可绑定）。

---

## F-CM-01 客户端可无上限增长静态描述缓存（knownDescriptions / knownPrimaries / primariesConversion）— 内存耗尽 + O(N²) CPU DoS

**严重程度：High**

**位置**
- `src/helpers/cm/ColorManagement.cpp:18-20` — 三个 `static` 全局容器：`knownPrimaries`（vector<UP>）、`knownDescriptions`（vector<UP>）、`primariesConversion`（map<(src,dst),CMatrix3>）。
- `src/helpers/cm/ColorManagement.cpp:46-56`（`CPrimaries::from`：线性去重扫描 + `emplace_back`，**永不清理**）、`:95-105`（`CImageDescription::from` 同样只增不减）、`:80-85`（`convertMatrix` 按 id 对缓存矩阵）。
- 客户端输入入口：`src/protocols/ColorManagement.cpp:502-560`（`create_parametric_creator` 的 `create` 请求 → `CImageDescription::from(m_settings)` @ `:551`）。

**攻击路径**
恶意客户端 bind `wp_color_manager_v1` → 循环 N 次：`create_parametric_creator` → `set_tf_named(GAMMA22)` → `set_primaries(8 个随机 int32)` → `set_luminances(随机值)` → `create`。每次唯一组合在 `CImageDescription::from` 线性扫描未命中后**永久追加**一个条目（`knownDescriptions.size()+1` 作 id，永不复用/淘汰）。primaries 部分同时向 `knownPrimaries` 追加唯一 `CPrimaries`（含 toXYZ 矩阵），渲染/硬件 CM 再对每对 (source,target) id 计算并缓存 `primariesConversion` —— 该 map 随唯一 primaries 数呈 **O(N²)** 增长。即使客户端断开、销毁全部协议对象，这些条目仍驻留（静态所有权，不随客户端资源销毁）。

**PoC 思路**
libwayland 客户端脚本：`for i in 0..1e6 { create_parametric; set_primaries(i, i^0x5f3759df, ...8 args); create; destroy }`。观察 Hyprland RSS 线性增长；同时每次 `create` 的 O(N) 线性扫描使总 CPU 为 O(N²)，单连接即可把合成器主线程饿死（Wayland 请求串行处理）。

**影响**：本地任意 Wayland 客户端导致合成器内存耗尽、事件循环卡死 → compositor 整体 DoS（所有客户端黑屏/无响应）。

**修复建议**：给 `knownDescriptions`/`knownPrimaries`/`primariesConversion` 加 LRU 淘汰与每客户端配额（id 需单调不复用或以指针为 key）；`create` 时按 `wl_client` 统计唯一描述数并在超限后返回 `unsupported_feature`；考虑把描述所有权挂到创建它的客户端资源生命周期上。

---

## F-CM-02 `buildHardwareCM` 的重型 CPU 校验可被客户端每帧触发 — 渲染线程 CPU DoS

**严重程度：Medium**（需用户开启 `render:non_shader_cm` = always/ondemand，但该选项是本 fork 特性开关，属正常配置）

**位置**
- `src/output/Monitor.cpp:2672-2723`（`hardwareCMFor`）：`:2700` 用客户端表面描述调 `CImageDescription::from(effective)`（又向 F-CM-01 的全局缓存注入），`:2705` 缓存未命中即调 `buildHardwareCM`。缓存为**单条目**（`m_hardwareCMCache`，key 含 `sourceId`）。
- `src/helpers/cm/HardwareCM.cpp:128-129`（两个 LUT 构建，每条目 pow/exp）与 `:63-80,152-158`：`verificationSamples()` 生成 ~2514 个样本，每个样本执行 2 次 `convertColor`（CPU 端完整 TF 解码/矩阵/tonemap pow/log）+ 矩阵求逆。
- 触发点：`CMonitor::attemptDirectScanout` 每帧调用 `scanoutCMNeedsUpdate(PSURFACE)` / `stageScanoutCM(...)`（`src/output/Monitor.cpp:2764-2795`）。

**攻击路径**
恶意客户端全屏独占一个输出（满足 DS 其他条件），为其 `wp_color_management_surface_v1` 每次 commit 前用一个**新的唯一 parametric 描述**（`set_primaries` 换一字节即可）替换图像描述 → `sourceId` 每帧变化 → 单条目缓存每帧 miss → 渲染线程在主提交路径上同步执行完整 `buildHardwareCM`（数千次超越函数运算 + 全 LUT 构建）。两个描述来回切换即可确定性实现。

**PoC 思路**
weston-clients 风格程序：绑定 CM 协议，创建 PQ/GAMMA22 表面，交替 commit 两个仅 primaries 末位不同的描述；`perf top` 可见 `convertColor`/`pow` 热点，帧率显著下降。

**影响**：合成器渲染线程被客户端输入饿死，全桌面卡顿/掉帧；与 F-CM-01 叠加时缓存重建还伴随全局容器增长。

**修复建议**：把 `verificationSamples` 结果按 (source,target,params) 哈希做**结果级**缓存（含负缓存）；限制每秒重建次数或改为异步预校验+上一版兜底；DS 路径优先用描述 id 做快速拒绝路径。

---

## F-CM-03 `set_primaries` / `set_mastering_display_primaries` 无范围校验，未校验的 masterin g 元数据直接进入 HDMI infoframe

**严重程度：Medium**

**位置**
- `src/protocols/ColorManagement.cpp:619-632`（`set_primaries`：8 个 int32 直接 `/1e6`，接受负值与 >1e6 值，spec 要求 0..1,000,000 且无校验）。
- `src/protocols/ColorManagement.cpp:647-664`（`set_mastering_display_primaries`：同样无校验；且**从不验证**目标音量包含于主音量——代码里自己留有 FIXME `:661-664`）。
- `src/protocols/ColorManagement.cpp:666-677`（`set_mastering_luminance`：`max_lum <= min` 单位混合检查——`min` 已 `/10000` 而 `max_lum` 是原始 cd/m²，几乎恒通过；无 PQ 规则 max≥min+10000 等约束）。
- 汇点：`src/helpers/cm/HDRMetadata.cpp:24-26,38-52`（`createHDRMetadata` 用 `settings.masteringPrimaries`/`masteringLuminances` 填 `hdr_output_metadata`）；`src/render/Renderer.cpp:2521-2528`（DS HDR 路径把**客户端表面的**描述直接 `setHDRMetadata` 下发到内核/HDMI infoframe）。

**攻击路径**
恶意客户端：创建描述 `set_tf_named(ST2084_PQ)` + `set_mastering_display_primaries(8×INT32_MAX)` + `set_mastering_luminance(0, 65535)`，提交到全屏表面 → 该表面获得 DS + `wantHDR`（`render:cm_auto_hdr` 默认下 HDR 判定 `isHDR()` 只看 TF/亮度字段，客户端可自封 HDR）→ 合成的 `hdr_output_metadata`（含 eotf=2、max_mastering=65535、白点 x/y=65535 等越界值）原样进内核 DRM 属性 → 显示器按错误 EOTF/母显音量渲染。to16Bit/toNits 的 clamp 只防越界写入，不拒绝语义非法值。

**影响**：客户端可远程操纵 HDMI/DP 面板的 HDR 元数据（过压 EOTF、错误母显峰值），轻则全局色偏/闪屏，重则对支持激进 HDR 增益的显示器造成可见损害；绕过规范要求的描述有效性验证。

**修复建议**：`create` 时对主/母显 primaries 强制 0 ≤ x,y ≤ 1e6、x+y ≤ 1e6 类几何约束并拒绝负值；`set_mastering_luminance` 校验单位与 `max ≥ min + 10000`；DS 元数据下发前用 EDID 能力集交叉检查。

---

## F-CM-04 `set_luminances` 接受病态 reference/min/max → shader 除零与 inf/NaN 颜色流水线

**严重程度：Medium**（GPU 端以视觉损坏为主，跨驱动行为未定义）

**位置**
- `src/protocols/ColorManagement.cpp:633-645`：`set_luminances` 仅校验 `reference_lum <= min` 和非 PQ 时 `max_lum <= min`。客户端可取 `min_lum=0, max_lum=1, reference_lum=1`（1 cd/m² 参考白通过），或 `min` 接近 `max`（float 精度内相等）；PQ 时 `create` 里 `luminances.max = round(min + 10000)`（`:525`）不检查 min 自身合法性。
- `src/helpers/cm/ColorManagement.cpp:240-247`（`encodingMinLuminance/encodingMaxLuminance` 直接透传这些值）与 `:310` 附近 `referenceLuminanceScale = target.reference / source.reference`（source.reference 可为 1 → 200× 放大；可为 0 才回退）。
- 汇点（客户端值进入 uniform，见 `src/render/OpenGL.cpp:1283-1292`）：
  - `src/render/shaders/glsl/tonemap.glsl:59-62`：`srcRange = pqEncode(srcMax) - pqEncode(srcMin)`，当缩放后 `max-min < 1e-6·10000` 或两者 PQ 编码同为 1.0（clamp）时 `srcRange == 0` → `minLum/maxLum/E1` 除零 → inf/NaN。
  - `tonemap.glsl:65-66`：`KS = 1.5·maxLum - 0.5`，`maxLum ≥ 1` 时 `(1.0 - KS)` 除零/负。
  - `src/render/shaders/glsl/cm_helpers.glsl:88-92`：`tfInvHLG` 的 `(display - range.x) / (range.y - range.x)` 与 `hlgBeta` 的 `range.x/range.y`（`range.y==0` 除零，HLG 目标 peak 可被 `encodingMaxLuminance()` 配成 0：`luminances.min=0, HDR_MAX=10000` 之外，BT1886 时 `range.y==range.x` 均可发生）。
  - `cm_helpers.glsl:141,147`：`tfHLG` 的 `/ range.y` 与 `/ (1.0 - beta)`（beta→1 即 range.min/range.max→1 时除零）。
  - `cm_helpers.glsl:117-118,168-169`：`tfInvBT1886/tfBT1886` 的 `(range.y - range.x)` 与 `(white - black)` 双除零。
- CPU 镜像同样受影响（`src/helpers/cm/ColorManagement.cpp` tonemap/HLG 路径），产出的 inf 经 `HardwareCM.cpp:26-30` clamp 后进入 LUT（被 F-CM-02 的验证掩盖，可能给出错误通过判定）。

**攻击路径**
客户端创建 PQ 描述：`set_luminances(0, 10000000, 1)`（min≈max 场景用大 min 小差值即可）→ 提交给表面 → 渲染该表面时 `getCMSettings`（`src/render/Renderer.cpp:2121-2160`）把这些值乘 `referenceScale` 后作为 `srcTFRange/maxLuminance` uniform 下发 → fragment shader 除零。

**PoC 思路**
基于 audit-reports/wc-color-management.c 扩展：构造退化 luminance 组合渲染满屏色块，截屏对比发现输出 NaN→0 或 inf→饱和条带；在无 NaN 保证的移动 GPU（部分 Mali/旧 Adreno 驱动 NaN 不保证）上验证可能产生闪烁/整层丢失。

**影响**：任意客户端可破坏其它窗口共享的输出渲染（全屏损坏、频闪）；跨驱动行为未定义。

**修复建议**：协议层强制物理范围：`0 ≤ min < max`、`reference ∈ [min, max]` 且 `max-min` 不小于 float 相对精度；shader 端对 `range.y - range.x`、`srcRange`、`1-KS`、`1-beta`、`white-black` 加 `max(ε, ·)` 守卫，并在 CPU 同步。

---

## F-CM-05 SCMSettings 缓存键遗漏关键输入；`clearCMSettingsCache` 无人调用 — 陈旧色彩配置粘滞

**严重程度：Low**（正确性→安全边界：可让旧配置（如 tonemap 模式、sdr 亮度）在配置变更后继续作用于客户端内容）

**位置**
- `src/render/Renderer.cpp:2083-2085`：`clearCMSettingsCache` 定义后**全仓库无任何调用**（grep 仅见定义/声明）。缓存仅在 `CRenderContext::reset()`（`src/render/Context.cpp:77`，输出销毁/后端重建时）清空。
- `src/render/Renderer.cpp:2092-2096`：缓存 key = `srcDescId/dstDescId/surfacePtr/modifySDR/sdrMinLuminance`；但 settings 值还依赖 `ctx.m_data.pMonitor->m_sdrBrightness/m_sdrSaturation`（`:2124,2160-2161`）、`tonemapMode`（`:2137-2145`）与 config `render:cm_tonemap_mode`/`render:cm_enabled` —— 均不在 key 内。
- fork 还把 `sdrMaxLuminance` 从 key 中移除（diff @ getCMSettings），扩大了陈旧面。
- `surfacePtr` 为裸 `void*`（`ctx.m_data.surface.get()`，`:2091`）：表面销毁后地址复用可产生跨表面陈旧命中（ABA）。

**攻击路径**
受害者用户 `hyprctl keyword monitor X,sdr_brightness 2` 或改 `render:cm_tonemap_mode` 后，只要输出未被移除重建，客户端内容继续沿用旧的 SCMSettings uniform；恶意客户端可先诱导建立某种缓存条目（特定表面+描述），再利用地址复用让后续表面继承其渲染参数。

**修复建议**：恢复对 `clearCMSettingsCache` 的调用（monitor rule 应用、config 重读、`updateMonitorsVisible`），把 brightness/saturation/tonemapMode/相关 config 版本号纳入 key，用弱指针或代际计数替代裸指针。

---

## F-CM-06 新激活的 feedback/首选描述事件路径包含未守卫的 subsurface 解引用（继承代码被 fork 接通）

**严重程度：Medium**（潜在 SIGSEGV，需时序 PoC）

**位置**
- `src/protocols/core/Compositor.cpp:705-723`：`getPreferredImageDescription()` 中 `sc<CSubsurfaceRole*>(parent->m_role.get())->m_subsurface.lock()` 的返回值与 `subsurface->t1Parent()` 均未判空即解引用；同文件 `:178` 对 `t1Parent()` 却是有守卫的，证明其可空。
- fork 把该路径从死代码变为可达：`src/Compositor.cpp`（diff）`shouldChangePreferredImageDescription()` 由恒 `false` 改为 `true`；`src/protocols/ColorManagement.cpp:898-901`（`onImagePreferredChanged` 遍历 feedback）与 `:903-923`（`onMonitorImageDescriptionChanged` 也遍历 feedback）→ `onPreferredChanged`（`:411-426`）→ `getPreferredImageDescription()`；`get_preferred`/`get_preferred_parametric`（`:353-397`）同样直接调用。
- 事件源在客户端手里：`m_listeners.enter/leave`（`:402-403`）使每次 `wl_surface` enter/leave（鼠标移动、输出变化）触发广播。

**攻击路径**
客户端创建 subsurface，对 subsurface 调 `get_surface_feedback`；在 `wp_subsurface.destroy` 之后、role 尚未复位的窗口内触发 enter/leave 或 `hyprctl` 改 monitor 描述 → `m_subsurface.lock()` 返回空 → `subsurface->t1Parent()` 空指针 → 合成器崩溃（远程 DoS）。

**PoC 思路**
audit-reports/wc-color-management.c 基础上：创建 parent+sub，`get_surface_feedback(sub)`，销毁 sub 对象但保留 parent，客户端高频重映射/移动触发行 enter/leave，同时并行 `hyprctl keyword monitor` 改 HDR 参数。

**修复建议**：`getPreferredImageDescription` 对 `lock()` 与 `t1Parent()` 判空并回退默认描述；`onPreferredChanged` 全包 `if (!m_surface) return;` 已有，但被调函数内部仍会解引用。

---

## F-CM-07 id→对象查找仅靠 ASSERT 且 id==0 时下标回绕（潜在越界读）

**严重程度：Low**（当前无调用点，属新引入的 API 地雷）

**位置**
- `src/helpers/cm/ColorManagement.cpp:107-110`：`CImageDescription::from(uint64_t id)`：`ASSERT(id <= size)` 后 `knownDescriptions[id - 1]` —— **id==0 满足条件却回绕为 `[-1]`**（size_t 巨值 → vector 越界读）；release 构建（NDEBUG）下 ASSERT 被编译掉，越界条件更宽。
- `src/helpers/cm/ColorManagement.cpp:63-65`：`CPrimaries::from(uint32_t)` 同构。

**攻击路径**
目前 `m_id` 从 1 起、无外部入口传 id；但 fork 新增的 64 位 id 拆分逻辑（`onPreferredChanged` 的 hi/lo，`protocols/ColorManagement.cpp:417-422`）暗示未来会以 id 反查。任何以客户端可控值调用这两个重载的代码即刻变成 OOB 读。

**修复建议**：`if (id == 0 || id > size) return nullptr;` 用硬校验替代 ASSERT。

---

## F-CM-08 CM surface 销毁后 committed 图像描述继续影响输出状态（inert 语义违规 + 陈旧客户端数据进内核）

**严重程度：Low**

**位置**
- `src/protocols/ColorManagement.cpp:279-286`：`wp_color_management_surface_v1.destroy` 只清 **pending**（`setPendingImageDescription(surface, {})`），`m_current.imageDescription` 保留至下次 commit。
- `src/protocols/core/Compositor.cpp:523-525`：fork 把 `findWithCM` 的判定从“存在活动 CM 对象”改为“`m_current.imageDescription` 非空”。
- 消费点：`src/render/Renderer.cpp:2512-2530`（`SURF->m_current.imageDescription->value()` 驱动 auto-HDR 与 DS HDR infoframe）、`src/output/Monitor.cpp:2160-2180` 附近 `attemptDirectScanout` 的 DS 资格判定。

**攻击路径**
客户端销毁 `wp_color_management_surface_v1` 对象（协议上此后应视为 inert）而不 commit 新状态：其已提交描述仍被渲染器当作有效 HDR 声明 → 保持 `wantHDR`、驱动 `createHDRMetadata` 下发客户端元数据、影响 DS 资格——越过对象生命周期影响他人（同表面由新客户端接管时残留）。

**修复建议**：destroy 时同时清空 committed 描述（或标记 `rejected` 强制下次 commit 生效），`findWithCM` 恢复检查 `m_colorManagement` 活跃性。

---

## F-CM-09 ICC creator 死代码与资源管理隐患（fd 不关闭 / user-data 类型混淆面）

**严重程度：Low（当前不可达）/ Info**

**位置**
- `src/protocols/ColorManagement.cpp:135-136`：`create_icc_creator` 一律报 `unsupported_feature` → `CColorManagementIccCreator`（`:436-484`）不再可达，但其构造函数、`set_icc_file`（`:478-482` 保存 `fd/offset/length`，**不关闭 fd、不校验 offset+length、不检查 fd 可读性**）与 `create`（`:444-474`，两处 "FIXME actually check"）仍在。一旦后续接回（`get_preferred_parametric` 已按 ICC 输出走线性回退，`:384-387`），即为 fd 泄漏 DoS + 任意 fd 读原语。
- `src/protocols/ColorManagement.cpp:294-302`：`set_set_image_description` 把 `wl_resource_get_user_data(image_description)` 强转 `CWpImageDescriptionV1*` 仅做判空（“FIXME check validity”）——客户端可将**任意接口对象 id** 作为 `image_description` 参数传入（libwayland 不校验 `n` 参数的接口类型），依赖后续 `find_if` 指针比对侥幸免于类型混淆；若哪天解引用 `PO` 即成 UAF/类型混淆利用面。

**修复建议**：删除不可达 ICC creator 或补全 fd 校验/RAII（`CFileDescriptor`）；用 `wl_resource_instance_of` 校验接口类型后再取 user_data。

---

## 汇总

| ID | 严重程度 | 一句话 |
|----|---------|--------|
| F-CM-01 | High | 静态 knownDescriptions/knownPrimaries/primariesConversion 被客户端 parametric create 无界填充 → 内存耗尽 + O(N²) 卡死主线程 |
| F-CM-02 | Medium | DS 路径每帧可强制重建硬件 CM（~5000 次 convertColor 校验）→ 渲染线程 CPU DoS |
| F-CM-03 | Medium | set_primaries/set_mastering_* 无范围校验，恶意 HDR 元数据直通 HDMI infoframe |
| F-CM-04 | Medium | 病态 luminances/reference 通过校验 → shader 多处除零、inf/NaN 颜色流水线 |
| F-CM-05 | Low | SCMSettings 缓存键不完整且 clearCMSettingsCache 从未调用 → 陈旧色彩 uniform 粘滞 + 裸指针 ABA |
| F-CM-06 | Medium | fork 激活的 feedback 事件路径撞上未判空的 subsurface t1Parent 链 → 潜在崩溃 |
| F-CM-07 | Low | id 反查 `[id-1]` 下标回绕，ASSERT 在 release 下失效 |
| F-CM-08 | Low | CM surface 销毁后 committed 描述继续驱动 auto-HDR/DS 元数据 |
| F-CM-09 | Low/Info | ICC creator 死代码持有未校验 fd；user-data 强转无接口校验 |

（与旧报告的关系：旧 #1 反馈空指针崩溃、#4 PQ 规则、#17 NaN 已修复；本清单为 fork 新增代码或 fork 新打通路径上的问题。）
