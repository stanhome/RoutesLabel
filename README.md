# RoutesLabel

跨平台地图路线绘制 Demo（macOS / Linux），基于 Vulkan + MoltenVK。

当前阶段目标：搭建工程骨架，跑通图形管线，渲染一个彩色三角形。后续阶段在此骨架上扩展地图渲染与基于 Compute Shader 的路线并行运算。

## 技术栈

| 模块 | 选型 |
|---|---|
| 语言 | C++17 |
| 图形 API | Vulkan 1.2（macOS via MoltenVK → Metal） |
| 窗口 / 输入 | GLFW（git submodule） |
| 数学 | GLM（git submodule, header-only） |
| 构建 | CMake（默认 Unix Makefiles，无需 Ninja） |
| Shader | GLSL → SPIR-V，构建期由 `glslangValidator` / `glslc` 编译 |

## 依赖安装

### macOS

```bash
brew install vulkan-headers vulkan-loader molten-vk vulkan-validationlayers \
             glslang shaderc cmake
```

或安装 [LunarG Vulkan SDK for macOS](https://vulkan.lunarg.com/sdk/home)，并 `source setup-env.sh`（推荐）。

### Linux (Ubuntu / Debian)

```bash
sudo apt install build-essential cmake \
                 libvulkan-dev vulkan-validationlayers vulkan-tools \
                 glslang-tools spirv-tools \
                 libglfw3-dev xorg-dev
```

或安装 [LunarG Vulkan SDK for Linux](https://vulkan.lunarg.com/sdk/home)。

## 拉取代码（含 submodule）

```bash
git clone <repo-url> RoutesLabel
cd RoutesLabel
git submodule update --init --recursive
```

或克隆时一并拉取：`git clone --recurse-submodules <repo-url>`

## 构建与运行

```bash
cmake -S . -B build
cmake --build build -j

# Linux：直接跑（系统包安装好 Vulkan 驱动/loader 即可）
./build/bin/routes_label
# 也可以用 tools/run-linux.sh，支持 LunarG SDK 与 validation 开关
./tools/run-linux.sh                       # 普通运行
ROUTES_VALIDATION=1 ./tools/run-linux.sh   # 强制开启 validation layer

# macOS：用 tools/run-mac.sh，会自动设置 MoltenVK ICD / Validation Layer / dyld 搜索路径
./tools/run-mac.sh
```

可选：
- 切换到 Release 构建：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- 强制开启 validation layer：`-DROUTES_ENABLE_VALIDATION=ON`

> macOS 下不通过 `tools/run-mac.sh` 直接运行时，需要手动设置以下环境变量让 Vulkan loader 找到 MoltenVK 与 validation layer：
> ```
> export VK_DRIVER_FILES=$(brew --prefix)/etc/vulkan/icd.d/MoltenVK_icd.json
> export VK_LAYER_PATH=$(brew --prefix)/share/vulkan/explicit_layer.d
> export DYLD_LIBRARY_PATH=$(brew --prefix)/lib:$DYLD_LIBRARY_PATH
> ```

## 目录结构

```
RoutesLabel/
├── CMakeLists.txt            顶层构建脚本
├── cmake/
│   └── CompileShaders.cmake  GLSL→SPIR-V 编译辅助函数
├── tools/
│   ├── run-mac.sh            macOS 运行包装脚本（MoltenVK ICD / Validation Layer / dyld 路径）
│   └── run-linux.sh          Linux 运行包装脚本（LunarG SDK 集成 + validation 开关）
├── third_party/
│   ├── glfw/                 GLFW（submodule）
│   └── glm/                  GLM（submodule）
├── shaders/                  GLSL 源（构建期编译为 .spv）
├── src/
│   ├── main.cpp              入口
│   ├── app/                  Application：组合 Window + Renderer，主循环
│   ├── platform/             Window：GLFW + Vulkan Surface
│   ├── rhi/                  Vulkan 资源 RAII 封装（Instance / Device / Swapchain ...）
│   ├── renderer/             高层渲染逻辑（TriangleRenderer，后续 MapRenderer ...）
│   └── utils/                Log、FileSystem 等通用工具
└── doc/                      设计文档
```

## 设计要点

- **分层**：`Platform` → `RHI` → `Renderer` → `App`，单向依赖，便于替换/扩展
- **RAII**：所有 Vulkan handle 由对应类的析构函数销毁，遵循 Vulkan 销毁顺序
- **Compute 预留**：Queue 选择、Buffer usage 位、Pipeline 抽象都为后续 compute pipeline 留好接口
- **跨端零分支**：唯一的平台分支集中在 `rhi/Instance.cpp`（macOS portability flag），App 层无任何 `#ifdef`
