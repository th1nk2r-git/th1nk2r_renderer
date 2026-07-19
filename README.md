# th1nk2r_renderer

一个使用 C++20 编写的轻量级 Vulkan 渲染器练习项目。项目通过 GLFW 创建窗口，使用 Vulkan-Hpp RAII 接口管理图形资源，并通过 Slang 编译着色器。目前程序会创建一个 `1200 × 800` 的可缩放窗口并绘制彩色三角形。

## 当前功能

- Vulkan 1.4 实例、调试消息与窗口 Surface 初始化
- 自动选择支持图形与呈现队列的物理设备
- Vulkan 逻辑设备与交换链管理
- Render Pass、Framebuffer 与图形管线创建
- 双帧并行的命令缓冲与同步对象管理
- Slang 顶点/片段着色器自动编译为 SPIR-V
- 窗口缩放、最小化恢复及交换链重建
- 优先使用 Mailbox 呈现模式，不可用时回退到 FIFO

## 技术栈

| 组件 | 用途 |
| --- | --- |
| C++20 | 项目开发语言 |
| Vulkan-Hpp / Vulkan RAII | Vulkan API 与资源生命周期管理 |
| GLFW | 窗口、事件和 Vulkan Surface |
| GLM | 图形数学库 |
| Slang | 着色器语言与 SPIR-V 编译器 |
| Xmake | 项目配置、依赖管理与构建 |
| MSVC | Windows C++ 工具链 |

## 环境要求

当前构建配置面向 **Windows x64 + MSVC**。开始前请准备：

- Visual Studio 2022 或 Build Tools，并安装“使用 C++ 的桌面开发”工作负载
- [Xmake](https://xmake.io/)
- 支持 Vulkan 1.4 的显卡、驱动与 Vulkan Runtime
- 可在命令行中调用的 `slangc`；项目构建脚本会用它编译 `shaders/` 中的着色器
- 推荐安装 Vulkan SDK，以获得验证层及常用调试工具

可以使用以下命令检查关键工具：

```powershell
xmake --version
slangc -version
```

> `vulkansdk`、`glfw` 和 `glm` 已在 `xmake.lua` 中声明，首次配置或构建时 Xmake 会解析并安装相应的软件包。

## 构建与运行

在项目根目录打开 PowerShell。

### Debug

```powershell
xmake f -m debug
xmake
xmake run application
```

### Release

```powershell
xmake f -m release
xmake
xmake run application
```

也可以直接从输出目录运行：

```powershell
Set-Location .\bin
.\application.exe
```

构建产物位于 `bin/`，编译后的着色器位于 `bin/spv/`。程序使用相对路径读取着色器，因此手动启动时应将工作目录设为 `bin/`。

## 项目结构

```text
th1nk2r_renderer/
├─ include/                   # 公共头文件
│  ├─ core/                   # 应用生命周期
│  ├─ gfx/
│  │  ├─ device/              # Vulkan 实例、Surface、设备
│  │  ├─ frame/               # 帧资源与同步
│  │  ├─ pipeline/            # 着色器模块与图形管线
│  │  └─ swapchain/           # 交换链及其相关资源
│  ├─ platform/               # GLFW 窗口封装
│  └─ render/                 # 渲染器接口
├─ src/                       # 对应模块的实现
├─ shaders/
│  ├─ vertex/                 # Slang 顶点着色器
│  └─ fragment/               # Slang 片段着色器
├─ bin/                       # 可执行文件与 SPIR-V（构建生成）
└─ xmake.lua                  # Xmake 项目配置
```

## 渲染流程

每一帧大致执行以下步骤：

1. 等待当前帧的 Fence。
2. 从交换链获取下一张可用图像。
3. 记录 Render Pass、Viewport、Scissor、管线绑定和三角形绘制命令。
4. 将命令缓冲提交到图形队列。
5. 等待渲染完成后，将图像提交到呈现队列。
6. 当窗口尺寸变化或交换链失效时，等待有效窗口尺寸并重建交换链相关资源。

当前三角形顶点由顶点着色器根据 `SV_VertexID` 生成，因此还没有创建实际的顶点缓冲区。

## 着色器

构建结束后，`xmake.lua` 会查找以下目录中的 `.slang` 和 `.hlsl` 文件：

- `shaders/vertex/`
- `shaders/fragment/`
- `shaders/compute/`（目录存在时）

每个着色器默认使用 `main` 作为入口，并输出到 `bin/spv/<文件名>.spv`。新增着色器时，请避免不同阶段使用相同文件名，否则输出文件可能互相覆盖。

## 常见问题

### 构建时提示找不到 `slangc`

确认 Slang 已安装，并将包含 `slangc.exe` 的目录加入 `PATH`。重新打开终端后执行 `slangc -version` 验证。

### 程序提示无法读取 `.spv` 文件

先完整执行一次 `xmake`，并确认 `bin/spv/vertex.spv` 和 `bin/spv/fragment.spv` 已生成。手动运行程序时，需要从 `bin/` 目录启动。

### 找不到合适的 GPU

请更新显卡驱动，并确认设备支持 Vulkan 1.4、图形队列、窗口呈现以及 `VK_KHR_swapchain` 扩展。

### 未启用 Vulkan 验证层

Debug 构建会尝试启用 `VK_LAYER_KHRONOS_validation`。若验证层不可用，程序会输出警告并继续运行；安装 Vulkan SDK 后即可获得该验证层。

## 开发状态

项目仍处于早期开发阶段，目前主要用于验证 Vulkan 初始化、交换链、图形管线和基础帧同步流程。模型加载、顶点/索引缓冲、纹理、深度测试、描述符以及场景系统等功能尚未实现。
