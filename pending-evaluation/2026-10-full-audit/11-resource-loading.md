# 11 — 不可信资源加载审计（图像解码 / 图标 / 光标 / Texture）

- 仓库：/home/wilf/data/Hyprland（fork，main @ 397cb0efb，v0.56.0 系）
- 审计域：`AsyncResourceGatherer`、图像解码链（jpeg/png/bmp/webp/avif/jxl/svg/lcms）、光标主题加载、Texture 构建
- 日期：审计执行于本地会话；仅静态阅读，未运行 PoC

## 0. 范围与架构事实（先行澄清）

1. **本 fork 中 `CAsyncResourceGatherer` 与全部图像解码器已外移至上位库 hyprgraphics 0.5.1**。
   - 仓库内仅剩薄封装：`src/render/AsyncResourceGatherer.hpp`（7 行，`#include <hyprgraphics/resource/...>` + 全局 `g_pAsyncResourceGatherer`）。
   - 实际代码在 nix store 源码副本：`HYPERGRAPHICS_SRC = ~/.cache/hymission-nix/nix/store/gy23wnbrkipx67mhiimshgpc0wrb9wqz-source`
     （即 Hypr-Ecosystem/hyprgraphics，系统安装版 `hyprgraphics-0.5.1`，`CMakeLists.txt:157` 强制依赖）。
   - 因此多数行号指向 vendor 库；修复需上游 PR + 版本 bump。
2. **GIF：不存在解码器**（hyprgraphics formats/ 仅有 Avif/Bmp/Jpeg/JpegXL/Png/Svg/Webp；任务书假设过时）。libpng 内部不涉 gif。
3. **lcms 只有一处使用**：`src/helpers/cm/ICC.cpp`，输入是 **monitor rule 配置指定的本地 ICC 文件**（`src/config/shared/monitor/Parser.cpp:277` → `src/output/Monitor.cpp:653`），客户端不可达。`wp-color-management-v1` 的 ICC creator 被直接拒绝（`src/protocols/ColorManagement.cpp:135-136`，`UNSUPPORTED_FEATURE`），**客户端提交的 ICC 不会被 lcms 解析** —— 这是本 fork 的重要正面结论。
4. **窗口 metadata → 图片路径：不存在此攻击面**。全仓 `CImageResource` 构造仅 1 处（`src/render/Renderer.cpp:1519`，默认壁纸 `wall{0,1,2}.png`，经 `resolveAssetPath` 限定 `ASSET_PATHS = DATAROOTDIR / /usr/share / /usr/local/share`，`src/render/types.hpp:15-21`，均为 root 属主目录、不含用户目录）。锁屏贴图同理（`Renderer.cpp:1722-1724`，`lockdead*.png`）。X11 侧不读 `_NET_WM_ICON`；Wayland 无 `xdg_toplevel.set_icon` 类协议进入文件加载器。
5. **窗口标题（完全客户端可控）只走纯文本渲染**：`src/render/decorations/CHyprGroupBarDecoration.cpp:314` → `IHyprRenderer::renderText`（`Renderer.cpp:1660` 重载，`pango_layout_set_text`，非 markup）。hyprgraphics 的 markup 解析路径（`TextResource.cpp:48`，`pango_parse_markup`）只被 debug overlay / error overlay / 通知（lua、插件、config 文本）调用，客户端标题不可达。

→ 真正的"恶意 X/Wayland 客户端"可达图像入口是 **wl_shm 光标/缓冲区像素**（F-RES-07），而解码器类缺陷属于"恶意文件落在资产/主题目录"这一较低权限触发面（本地文件投放、恶意包、共享可写目录），严重度按可达性下调，但内存破坏类仍须上游修复。

---

## 1. 发现列表

### F-RES-01（高危｜vendor）PNG 解码无尺寸上限 → 解压炸弹直接打爆合成器
- 位置：`$HG/src/image/formats/Png.cpp:100-102`（`loadPNG`）
  ```cpp
  rowPointers.resize(H);
  rawData.resize(W * H * 4);
  ```
  以及 `:123-126`（`if (!CAIROSURFACE)` 判空——cairo OOM 返回 error-status 表面而非 NULL，`cairo_image_surface_get_data` 随后返回 NULL，`memcpy` 空指针写）。
- 问题：`W/H` 取自 IHDR（libpng 允许至 2^31-1），未调 `png_set_user_limits`、无 `set_check_for_inefficiencies`、无维度上限。单张几十 KB 的 "PNG" 声明 65535×65535 → `rawData` 一次申请 ~16.1GB，随后 cairo surface 再申请同样大小（双份），且未捕获 `bad_alloc/length_error`——异常发生在 gatherer 工作线程 → `std::terminate` → **整个合成器（连同用户全部会话）死亡**。
- 攻击路径：恶意文件落在壁纸/锁屏资产查找目录（`/usr/share/hypr`、`/usr/local/share/hypr`）或任何调用 `CImage`/`CImageResource` 的插件路径；配合 `misc:disable_wallpaper` 翻转、锁屏进出等触发重载可反复攻击。
- PoC 思路：`convert -size 65535x65535 xc:black bomb.png`（或手工构造 67 字节 IHDR）；监视 `hyprland` RSS 直至 OOM/terminate。
- 修复：解码前 `png_get_image_width/height` 校验（如 ≤8192×8192 且 `W*H ≤ 64M`，用 `size_t` 乘法防截断）；`png_set_user_limits(png, W_MAX, H_MAX)`；`png_set_add_check`；以 `cairo_image_surface_get_stride` 逐行拷贝替代整块 `memcpy`；检查 `cairo_surface_status` 再取 data；worker 循环里捕获异常并标记资源失败。上游提 PR。

### F-RES-02（高危｜vendor）手写 BMP 解析器：多重整数溢出 + 栈溢出写 + 泄漏
- 位置：`$HG/src/image/formats/Bmp.cpp`
  - `:64` 头校验 `if (imageSize != (width * height * numberOfBitPerPixel / 8))` —— `width/height` 为 `uint32_t`，乘法全程 **32 位环绕**，攻击者可构造真值巨大的 w/h 使表达式回绕成任意 `imageSize`（配合 `:34` 的 `streamLength` 也是 `uint32_t`）。
  - `:113` `malloc(static_cast<size_t>(bitmapHeader.height * stride))` —— `height(u32) * stride(int)` 在 **32 位域内相乘后才 cast** → 小分配。
  - `:116` 24bpp 路径 `convertRgbToArgb(..., height*stride)` 按未溢出的目标尺寸写 → **堆溢出**（写入内容为 BMP 文件字节，攻击者可控）。
  - `:118` 32bpp 路径 `bitmapImageStream.read(reinterpret_cast<char*>(&imageData), bitmapHeader.imageSize)` —— 把文件数据读进**局部指针变量的地址**（`&imageData`，栈上 8 字节槽），`imageSize` 可达 MB 级 → **栈缓冲区溢出**，文件内容即写入数据。
  - `:111-121` 不支持的 bpp 分支 return 前不 `free(imageData)`；成功路径 `cairo_image_surface_create_for_data`（`:125`）引用 malloc 缓冲且永不 `free` → 每次加载 BMP 泄漏整幅图像。
- 攻击路径/ PoC：同 F-RES-01（BMP 落入资产目录即触发）；构造 `width=0x40000, height=0x8000, bpp=24` 使 `w*h*3 mod 2^32` 匹配 40 字节文件的 `imageSize` → 通过校验 → `malloc(height*stride mod 2^32 = 小值)` → 按真尺寸写 → 堆越界。32bpp 仅需一个合法头 + ≥1KB 数据即可踩 `:118`。
- 修复：整个解析器重写为 checked arithmetic（`__builtin_mul_overflow` 或 `uint64_t` 全程）+ 维度上限；32bpp 读取目标改为 `imageData`；用 `std::vector<uint8_t>` 替代 malloc/free；或干脆删除 BMP 支持（cairo 本身可载 bmp）。上游 PR。

### F-RES-03（中危｜vendor）JPEG：空文件 OOB 读、错误路径双泄漏、全局 jmp_buf
- 位置：`$HG/src/image/formats/Jpeg.cpp`
  - `:10` `jmp_buf bailoutBuf = {};` **非 static 全局**：外部链接符号污染 + 任何多线程并发解码互相踩踏 `longjmp`（当前 gatherer 单线程侥幸安全，属定时炸弹）。
  - `:29` `if (bytes[0] != 0xFF || ...)` —— 0 字节文件时 `bytes` 为空 vector，越界读 `data()[0]`（UB，通常段错误在 worker 线程 → 合成器崩）。
  - `:41` `setjmp` 命中（libjpeg error_exit 对所有畸形结构触发）后直接 return：`jpeg_destroy_decompress` 未调 → libjpeg 内部池泄漏；`cairoSurface` 未 `cairo_surface_destroy` → 表面泄漏。`:59` cairo 失败路径同样不 destroy。畸形 JPEG × N 次加载 = 线性泄漏。
  - 无尺寸上限（`output_width/height` ≤65535 → 16GB cairo 分配，炸弹面同 F-RES-01）；`:70` mime data 指向函数局部 `bytes`，函数返回后 surface 持有悬垂指针（本合成器只上 GL 纹理不读 mime，属潜伏 UAF）。
- PoC 思路：`truncate -s 0 x.jpg` 放资产目录 → 崩溃；fuzz 语料（jpeg-crashers）× 反复 `hyprctl reload` 类触发 → RSS 单调增长。
- 修复：空/小文件检查；`thread_local jmp_buf` 包进 C++ 局部结构；用 ScopeGuard 保证 `jpeg_destroy_decompress`/`cairo_surface_destroy` 全路径执行；mime data 用带 destroy 回调的堆拷贝；维度上限。

### F-RES-04（中危｜vendor）WebP：空文件 OOB 读；解码失败路径泄漏 surface
- 位置：`$HG/src/image/formats/Webp.cpp:20`（`bytes[0..3]` 无长度检查，空文件越界读）、`:58-59`（`WebPDecode` 失败 return，既不 `cairo_surface_destroy` 也不 `WebPFreeDecBuffer` → 每个畸形 webp 泄漏一张最大 16383² 的表面引用/句柄）、`:62`（mime data 指向局部 `bytes`，悬垂，同 F-RES-03）。
- PoC：0 字节 `.webp` 入资产目录；畸形 RIFF 反复加载观察泄漏。
- 修复：同 F-RES-03 套路（长度检查、RAII、上限）。

### F-RES-05（中危｜vendor）AVIF：错误路径不释放 heif handle/img；双重全尺寸分配且 memcpy 前不查 cairo 状态
- 位置：`$HG/src/image/formats/Avif.cpp:25-26、31-32`（`return std::unexpected` 前未 `heif_image_handle_release`/`heif_image_release` → 畸形 avif 泄漏）；`:35` `rawData.resize(width*height*4)`（`size_t` 无截断但无上限，16.1GB+ 又 16.1GB：libheif 解码产物 + rawData + cairo surface 三份）；`:55-58` `if (!CAIROSURFACE)` 无效判空后直接 `memcpy(cairo_image_surface_get_data(...))` —— OOM 时 get_data 为 NULL → 空指针写崩溃。
- PoC：`avif --size 65535x65535` 或复用 heif-crashers 语料。
- 修复：RAII 包装 handle/img；`cairo_image_surface_get_width/stride` 一致性校验 + status 检查 + 上限；避免 rawData 中转（直接 stride 逐行拷）。

### F-RES-06（中危｜vendor）AsyncResourceGatherer `await()` 丢唤醒 → 主线程永久挂起
- 位置：`$HG/src/resource/AsyncResourceGatherer.cpp:33-36 vs 67-69`
  主线程 `enqueue(res)` 后调 `await(res)`：若工作线程在两句之间完成 `render()`，它看到的 `awaitingCv` 为空（`:67`）而跳过 notify 且**不会补设 `awaitingEvent`**；随后主线程 `:33` 置 cv、`:35` 在 `awaitingEvent==false` 上无限等待 → **合成器主线程死锁**（比崩溃更难恢复）。`awaitingCv/awaitingEvent` 的读写也无锁保护，是数据竞争（UB）。触发窗口 = enqueue 返回到 await 进入之间的调度间隙；`Renderer.cpp:1704-1705`（锁屏/overlay 文本）高频调用，重启/高负载下可复现。
- PoC 思路：在 `enqueue/await` 间人为 `sched_yield`/插桩放大窗口，或压测 lockscreen 进出。
- 修复：单一 mutex 保护 `awaitingEvent`，worker 无条件置位 + notify，await 持锁等待谓词；或给 wait 加超时 + 轮询 `m_ready`。

### F-RES-07（高危｜in-repo）wl_shm 池无 SIGBUS 防护/未校验 memfd 密封 → 任意 Wayland 客户端一行代码崩合成器
- 位置：
  - `src/protocols/core/Shm.cpp:81、97`：`mmap(PROT_READ|PROT_WRITE, MAP_SHARED, client_fd)` 映射**客户端仍可 ftruncate 的 fd**；`:103-113 shmIsSizeValid` 仅在创建/resize 时 `fstat` 一次。
  - `src/protocols/core/Compositor.cpp:810-824`：光标 shm 缓冲逐帧 `memcpy` 自该映射（`beginDataPtr`，`Shm.cpp:65-66`）。
  - 全仓 `grep SIGBUS|sigbus|F_SEAL|memfd` **零命中**。
- 攻击路径：恶意客户端 `create_pool(1MB)` → `create_buffer`（合法 layout）→ attach 为光标面/普通面 → 随后 `ftruncate(own_fd, 0)`。合成器下次 damage/上传触碰被撕掉的页 → **SIGBUS → 进程死亡**。这是任务书威胁模型里唯一"远程客户端即时触发合成器崩溃"的资源入口（比解码炸弹更实际）。
- PoC 思路（文字）：toyland/wlcs 客户端建池后延迟 100ms ftruncate 池尾，同时提交带 damage 的 attach/commit；或光标面反复 pointer move 触发 `updateCursorShm` 拷贝。
- 修复：(a) 注册 SIGBUS handler + siglongjmp 围栏包住所有 shm 触碰点（wlroots 模式），并把出错 buffer 标记失效；或 (b) 创建池时强制 `fcntl(F_GET_SEALS)` 要求 `F_SEAL_SHRINK|F_SEAL_GROW`（Linux≥3.17），拒绝未密封 fd；两者最好都做。(c) 顺带：`beginDataPtr` 的 `bufLen = stride*h` 虽被 `isShmBufferLayoutValid`（`src/helpers/Format.cpp:53-83`，校验 `offset+stride*h ≤ poolSize`，正面确认）约束在映射内，但语义上应减 offset，防御性修正。

### F-RES-08（低中危｜in-repo）XCursorManager 手写 index.theme 解析可被冒名注入 → 主题名路径遍历
- 位置：`src/managers/XCursorManager.cpp:241-242`
  ```cpp
  if (line.rfind("Inherits", 8) != std::string::npos)   // 意图: starts_with
      std::string inheritThemes = line.substr(8);
  ```
  `rfind("Inherits", 8)` 判定的是"前 ~16 字符内**任意位置**出现 Inherits"，且无条件 `substr(8)`。攻击主题文件（如 `~/.icons/evil/index.theme` 或系统主题被篡改）可用 `xInherits=../../../tmp/attacker`、`FooInherits ...` 等行把**未经清洗的任意字符串**并入主题名集合（`:266-273` 只做 strip 空白），随后 `:303-307` 直接 `format("{}/{}/cursors", line, t)` 拼接 —— 主题名可含 `../` 与绝对路径成分，引导存在性探测与 libXcursor 解析任意目录下文件。`:316-322` 的继承收敛循环以 set 增长为界，恶意主题链可放大为 O(N×路径数) 的 stat 风暴（轻度 DoS）。
- 攻击路径：能写任一 XcursorLibraryPath 目录（含继承链上游主题）即获得"目录遍历探测 + 任意目录文件喂入 libXcursor"。
- PoC 思路：`~/.icons/BrokenTheme/index.theme` 写 `xInherits=../../..``/home/victim/steal`，观察 DEBUG 日志中 theme 路径枚举，验证被解析。
- 修复：改用 `g_key_file`（本文件已链 gio）解析 `Index Theme` 组；或 `starts_with("Inherits")` + 对每个主题名做白名单校验（拒绝 `/`、`..`、绝对路径、换行）。

### F-RES-09（低危｜in-repo）光标目录加载：跟随符号链接、失败重试不 rewind、像素拷贝信任 libXcursor
- 位置：`src/managers/XCursorManager.cpp:531`（`is_symlink` 显式放行 → `:538 fopen` 打开符号链接指向的**任意文件**喂 `XcursorFileLoadImages`）；`:543-547`（首次 `XcursorFileLoadImages` 已把 FILE* 读到 EOF，size=24 的第二次尝试必然空手——功能性 bug，也意味着 fallback 失效下畸形文件被解析两次）；`:196-197`（`memcpy(image.pixels.data(), xImage->pixels, w*h*4)`，`w/h` 来自库解析结果，完全信任 libXcursor 自洽性——libXcursor 历史有多起 ARC 头 `w*h*bpp` 溢出 CVE，版本过旧即演变成合成器内溢出）。
- 修复：拒绝 symlink（或 `openat`+`O_NOFOLLOW`/`realpath` 校验前缀在 cursors 目录内）；重试前 `fseek(f,0,SEEK_SET)` 或改用 `XcursorFilenameLoadImages`；对 `xImage->width/height` 加上限（如 ≤512）并在解析结果与 `nimage` 间做一致性检查；发布说明钉住 libXcursor 最低版本。

### F-RES-10（低危/纵深防御｜in-repo）客户端 ICC 上传路径：fd 永不关闭（当前为死代码，但属上线即泄漏）
- 位置：`src/protocols/ColorManagement.cpp:478-482`（`set_icc_file(fd,...)`：覆盖 `m_icc.fd` 时不 `close` 旧 fd；`destroy`/`create` 也不关 —— libwayland 收到的 fd 归接收方所有，客户端反复调用即**每调用泄漏 1 个 fd**）；`:448/465` 仅做 `<0||!length` 检查，未 mmap 前无 size/fstat 校验；`:462` "FIXME: Parse icc" 表明未来将把客户端字节喂给 lcms。
- 现状缓解：`:135-136` `create_icc_creator` 直接报 `UNSUPPORTED_FEATURE`，该类不可达 —— 按信息级记录。
- 修复：实现前预置：`setSetIccFile` 中 `if (m_icc.fd>=0) close(m_icc.fd);`，析构/close 请求时关闭；启用解析时限制 `length ≤ 10MB`、用 `memfd` 密封后的副本解析、只走 `cmsOpenProfileFromMem` 并校验 `cmsGetProfileType == DISPLAY_CLASS`。
- 关联（同文件面）：`src/helpers/cm/ICC.cpp:17-33 readBinary` 对 monitor rule 指定的 ICC 文件整读无大小上限（本地配置面，低）；`:296 cmsOpenProfileFromFile` 未使用受控 flags，依赖发行版 lcms（历史 CVE 面），建议随包更新 lcms 并对 `readVCGT16`（`:50-120`，边界处理目前自洽，`tableOff=18/20` 双偏移启发式已做尺寸校验）加语料回归测试。

### F-RES-11（信息｜vendor）SVG：librsvg 外部引用 = 本地文件读取/XXE 放大面；错误路径泄漏
- 位置：`$HG/src/image/formats/Svg.cpp:47-55、80-88`（`rsvg_handle_new_from_data` 渲染前无任何外联限制——librsvg 会解析 SVG 内 `xlink:href`/CSS `url()` 指向的 `file://` 与 http 资源、XML 实体展开（billion laughs）→ 一旦 SVG 变为客户端可达，即构成"任意文件内容渲染进纹理"式本地读取与网络外联原语）；`:45、50、55` 等错误 return 不释放 `cairoSurface`/`PCAIRO`/`handle`（每次失败泄漏一张目标尺寸表面 + cairo 上下文 + rsvg 句柄）。
- 当前可达性：SVG 仅从 root 属主资产目录加载；`CImageResource(svg, size)` 数据构造重载未见于仓内调用。维持信息级，列为**禁止把任何客户端字符串接入 `CImage`/`CImageResource` 构造**的红线依据。
- 修复：上游用 `rsvg_handle_new_from_data_unterminated` + `RsvgHandleFlags`/自定义 base URL 禁用外部资源与网络（librsvg≥2.52 支持 `rsvg_handle_set_base_gfile` 置 NULL 且关闭 `load-external-resources` 类行为，随版本核实）；全路径 RAII。

### F-RES-12（信息｜in-repo）正面确认清单（负结果同样入档）
- 窗口 metadata（title/app_id/seat 相关）无任何路径进入文件/图片加载器（§0.4）。
- 标题渲染走 `pango_layout_set_text` 纯文本（`Renderer.cpp:1670`），无 markup 注入；`CTextResource` 的 markup 路径输入均为配置/插件文本。
- wl_shm buffer 布局校验 `src/helpers/Format.cpp:53-83` 完整覆盖 `offset+stride*h ≤ poolSize`（含除法防溢出），`updateCursorShm`（`Compositor.cpp:787-828`）的矩形拷贝在该不变式下 int 运算不溢出、源目标同大小——本次唯一被正确加固的客户端像素入口；残余风险仅 F-RES-07。
- 壁纸文件名由 `PFORCEWALLPAPER`（clamp 0..2）拼接，非攻击者字符串；`resolveAssetPath` 不含 `~`、用户目录与任意参数。
- `magic` 内容嗅探（`$HG/src/image/utils/Format.cpp:28-69`，`MAGIC_MIME_TYPE|MAGIC_SYMLINK`）以内容而非扩展名定格式，防"改名骗过解码器分派"，正面；但 per-load `magic_load` 有 ~ms 级性能税（信息）。

---

## 2. 修复优先级建议

| ID | 严重度 | 位置 | 一句话 |
|---|---|---|---|
| F-RES-07 | 高 | in-repo Shm/Compositor | SIGBUS 围栏 + memfd 密封校验（唯一客户端即时触发的崩合成器路径） |
| F-RES-01/02 | 高 | hyprgraphics Png/Bmp | 解码前维度上限 + 重写 BMP（上游 PR，随 0.5.x bump） |
| F-RES-03/04/05/06 | 中 | hyprgraphics Jpeg/Webp/Avif/Gatherer | 长度检查、RAII 全路径清理、thread_local jmp_buf、await 竞态修复 |
| F-RES-08/09 | 低中 | in-repo XCursorManager | g_key_file 解析 + 主题名白名单 + O_NOFOLLOW |
| F-RES-10 | 低 | in-repo ColorManagement/ICC | fd 关闭前置化；ICC 解析启用前补校验 |
| F-RES-11 | 信息 | hyprgraphics Svg | 禁外联；红线：客户端字符串禁入 CImage |

验证建议：给 `tests/` 增加恶意资产回归（0 字节 jpeg/webp、超尺寸 IHDR、w*h 回绕 BMP、继承环 index.theme）；F-RES-06 可加 `enqueue+await` 压力单测。

*注：涉及上位库的行号基于本机 nix store 中 hyprgraphics 0.5.1 源码快照（`...gy23wnbrkipx67mhiimshgpc0wrb9wqz-source`），上游提 PR 时请按 Hypr-Ecosystem/hyprgraphics@对应 commit 复核行号。*
