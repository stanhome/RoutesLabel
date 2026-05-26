# RoutesLabel — Grid PCA Separation

> 面向**车载导航地图 Label Anchor** 的开源算法 Demo：在 3 条共享起止点的导航路径上，自动找出"互不歧义、视觉独占"的锚点 (anchor) 用于挂标签。
> 算法名：**Grid PCA Separation** —— Uniform Grid + 加权 PCA + 主轴投影区间不重叠度。
> 跨平台 C++17 / Vulkan 1.2（macOS 经 MoltenVK → Metal；Linux 原生）。CPU 与 **Compute Shader GPU 双实现**均已落地。

![main](doc/imgs/main.png)

---

## 1. 产品背景：车载地图的 Label Anchor 难题

车载导航在显示"3 条候选路线"时（推荐 / 较快 / 备选），需要在每条路径上各挂一个标签（如 "21 分钟 · 最快"），并满足三条**视觉硬约束**：

- **C1 独占性**：锚点必须落在该路径相对其他两路的"独占段"上 —— 不能贴在三条路重合的主干道；
- **C2 不歧义**：标签矩形不能与其他两条路径的任何线段相交 —— 否则乍一看分不清这个标签到底属于哪条路；
- **C3 互斥**：3 个标签矩形之间互不重叠。

在传统离线制图（GIS）里这是经典的 **Map Labeling** NP-hard 问题，求解器秒级出最优；但车载场景的边界完全不同：

| 维度 | 车载导航 |
|---|---|
| 触发频率 | 路径变化 / 视口缩放时**事件驱动** |
| 算力预算 | 单帧 ≤ 1~2 ms（要让出主线程跑业务逻辑与渲染） |
| 数据规模 | 3 条路径，每条 50~100 点，1080p~4K 屏 |
| 部署平台 | 高通 / 联发科车机芯片，移动 GPU 居多 |
| 可移植性要求 | 同一算法能跑在 CPU 上做 fallback，也能跑在 GPU 上节能 |

**Grid PCA Separation** 就是为这个边界设计的：CPU 单线程 < 1 ms 跑完，结构天然适配 compute shader，可平滑迁移到车机 GPU。

---

## 2. 算法：Grid PCA Separation

### 2.1 核心思想

> 把屏幕切成固定边长的 tile，在每个 tile 内做**加权 PCA 找主轴**，然后把三条路径**投影到主轴上**形成 3 个 1D 区间，**区间互不重叠的程度**就是该 tile 的"独占感"得分。

![grid-pca](doc/imgs/grid-pca.gif)

GIF 中可看到：随着三条路径形态变化，每个 tile 的颜色（score）实时更新，主轴方向（短线段）随 tile 内三路径分布旋转，最终选出 3 个 score 最高且互不相邻的 tile —— 它们就是 3 个 label 锚点的归属网格。

### 2.2 Pipeline（4 个 Stage）

```
┌──────────────┐  ┌────────────┐  ┌──────────────┐  ┌──────────────┐
│ Stage A      │  │ Stage B    │  │ Stage C      │  │ Stage D      │
│ Clip         │→ │ PCA        │→ │ Score        │→ │ Top-3 + NMS  │
│ 线段→tile    │  │ 加权协方差 │  │ separation × │  │ 选 3 个互不  │
│ (Amanatides- │  │ 2×2 闭式   │  │ density ×    │  │ 相邻的 tile  │
│  Woo DDA)    │  │ 特征分解   │  │ balance      │  │              │
└──────────────┘  └────────────┘  └──────────────┘  └──────────────┘
   per-segment      per-tile         per-tile          K=3 极小
   并行             workgroup        单线程            CPU readback
```

### 2.3 关键设计点

| 设计 | 解决的问题 |
|---|---|
| **固定边长 tile**（`s = min(W, H) / 40`） | 单格面积直观、调试可视化容易；与 label 尺寸完全解耦 |
| **弧长面密度加权** w_i = ℓ_i / Σℓ_j | 三路径采样点数不等（弯段密、直段稀）时，长度才是公平的"重要度"权重 |
| **PCA 主轴投影** | 用户直觉"最小二乘拟合一条线再求离散度"的精确数学化 |
| **区间不重叠度** separation = 1 − overlap/total | 比 Fisher 类间方差 / Calinski-Harabasz 几何意义更直观，不依赖类内方差大小 |
| **退化保护** | λ₂/λ₁ 过小（点云各向同性）/ tile 内 sub_seg < 3 → score = 0 自动剔除 |

### 2.4 性能（N=100 三路径 / 1080p / N_grid=40 → ~1600 tile）

| 实现 | 单帧耗时 | 备注 |
|---|---|---|
| CPU 单线程 | **< 1 ms** | 当前默认路径，事件驱动场景绰绰有余 |
| GPU compute（已实现，**未优化**） | **~0.5 ms** 估算上限 | 4 个 compute shader stage + indirect dispatch；尚未做 workgroup tuning / fp16 / scan-scatter 替换 atomic 等优化 |

---

## 3. GPU 移植：天然 compute-shader 友好

这是 **Grid PCA Separation 相对其他独占段识别算法（KD-tree 状态机 / Quadtree 自适应纯度）的核心优势**：

| 维度 | KD-tree + 状态机 | Quadtree + 纯度 | **Grid PCA Separation** |
|---|---|---|---|
| 并行粒度 | 沿路径 1D 扫描，**本质串行** | 树构建递归，依赖父节点 | **per-tile 完全独立** |
| 数据依赖 | 强（状态机：上一点 → 下一点） | 强（父 → 子） | **极弱**（仅 Stage A 写入 + Stage D 归约） |
| 内存访问 | KD-tree 随机指针追逐 | 树节点动态分配 | **uniform grid，coalesced** |
| 原子操作 | 多 | 多 | **少**（仅 Stage A ~1.8k 次 atomicAdd） |
| GPU 化难度（1–5） | 5 | 4 | **2** |

### 已落地

- ✅ `shaders/grid_clip.comp`  — Stage A：线段 → tile 裁剪（per-segment 并行 + atomicAdd append）
- ✅ `shaders/grid_pca.comp`   — Stage B：每 tile 一个 workgroup，shared memory 归约 + 2×2 闭式特征分解
- ✅ `shaders/grid_score.comp` — Stage C：separation / density / score（纯 ALU）
- ✅ `shaders/grid_scan.comp`  — 辅助 scan
- ✅ `shaders/grid_label.comp` — Stage D 配套：候选 label 评分
- ✅ `src/algo/GridGpu.{h,cpp}` — host 端 pipeline 编排、SSBO 管理、同步与回读
- ✅ `src/algo/GridCpu.{h,cpp}` — 等价 CPU 参考实现，作为 ground truth 与 GPU fallback

### 尚未做的优化（欢迎贡献 PR）

- ❌ 动态 workgroup size 选择（当前固定 64 / 32，未适配不同 GPU 架构）
- ❌ Stage A 的 "**count → prefix scan → scatter**" 两 pass 改造（参考 Merrill & Garland 2016），消除 atomic 竞争
- ❌ fp16 协方差累加（移动 GPU 带宽收益显著）
- ❌ Indirect dispatch 跳过空 tile（当前空 tile 也会被分发 wg）
- ❌ Stage D 纯 GPU bitonic Top-3，免去 CPU readback 的 1 帧延迟

实测表明，未优化版本已经能稳定满足 1~2 ms 预算；上述优化属于 **future-proof**（4K 屏 / 路径数 > 5 / 每帧重算等扩展场景）。

---

## 4. 技术栈

| 模块 | 选型 |
|---|---|
| 语言 | C++17 |
| 图形 / 计算 API | Vulkan 1.2（含 compute pipeline）；macOS 经 MoltenVK → Metal |
| 窗口 / 输入 | GLFW（git submodule） |
| 数学 | GLM（git submodule, header-only） |
| Shader | GLSL → SPIR-V，构建期由 `glslangValidator` / `glslc` 编译 |
| 调试 UI | Dear ImGui（已集成） |
| 构建 | CMake（默认 Unix Makefiles） |

---

## 5. 快速开始

### 5.1 依赖安装

**macOS**
```bash
brew install vulkan-headers vulkan-loader molten-vk vulkan-validationlayers \
             glslang shaderc cmake
```
或安装 [LunarG Vulkan SDK for macOS](https://vulkan.lunarg.com/sdk/home)，并 `source setup-env.sh`（推荐）。

**Linux (Ubuntu / Debian)**
```bash
sudo apt install build-essential cmake \
                 libvulkan-dev vulkan-validationlayers vulkan-tools \
                 glslang-tools spirv-tools \
                 libglfw3-dev xorg-dev
```
或安装 [LunarG Vulkan SDK for Linux](https://vulkan.lunarg.com/sdk/home)。

### 5.2 拉取代码（含 submodule）

```bash
git clone --recurse-submodules <repo-url> RoutesLabel
cd RoutesLabel
# 或者：git clone <repo-url> && git submodule update --init --recursive
```

### 5.3 构建与运行

```bash
cmake -S . -B build
cmake --build build -j

# Linux
./build/bin/routes_label
# 或用 wrapper（支持 LunarG SDK 与 validation 开关）：
./tools/run-linux.sh
ROUTES_VALIDATION=1 ./tools/run-linux.sh    # 强制开启 validation layer

# macOS（自动设置 MoltenVK ICD / Validation Layer / dyld 搜索路径）
./tools/run-mac.sh
```

可选构建开关：
- Release：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- 强制 validation layer：`-DROUTES_ENABLE_VALIDATION=ON`

> macOS 不通过 `tools/run-mac.sh` 直接运行时，需要手动设置：
> ```bash
> export VK_DRIVER_FILES=$(brew --prefix)/etc/vulkan/icd.d/MoltenVK_icd.json
> export VK_LAYER_PATH=$(brew --prefix)/share/vulkan/explicit_layer.d
> export DYLD_LIBRARY_PATH=$(brew --prefix)/lib:$DYLD_LIBRARY_PATH
> ```

### 5.4 切换 CPU / GPU 后端

运行时 ImGui Debug 面板提供 `Backend: CPU | GPU` 切换；两者输出严格一致（GPU 是 CPU 的并行实现），可逐 tile 对比 separation / score / 主轴方向。

### 5.5 Test cases

三条路线 **同起点、同终点**，数据在 `assets/cases/*.json`。

```bash
cmake -S . -B build
cmake --build build -j --target routes_label routes_label_cases
./tools/run-cases.sh -v                 # headless 全部
./tools/run-all-cases-visual.sh         # 打印每条 headless + GUI 命令
```

GUI 启动日志应含：`[RoutesRenderer] scene JSON: .../_active_case.json`。

| stem | headless | GUI |
|------|----------|-----|
| `trunk-fork-rejoin-crowding` | `./tools/run-cases.sh -v trunk-fork-rejoin-crowding` | `./tools/run-case-visual.sh trunk-fork-rejoin-crowding` |
| `three-routes-detour-same-od` | `./tools/run-cases.sh -v three-routes-detour-same-od` | `./tools/run-case-visual.sh three-routes-detour-same-od` |
| `vertical-staggered-corridor` | `./tools/run-cases.sh -v vertical-staggered-corridor` | `./tools/run-case-visual.sh vertical-staggered-corridor` |
| `parallel-horizontal-lanes` | `./tools/run-cases.sh -v parallel-horizontal-lanes` | `./tools/run-case-visual.sh parallel-horizontal-lanes` |
| `car-marker-fork-detour` | `./tools/run-cases.sh -v car-marker-fork-detour` | `./tools/run-case-visual.sh car-marker-fork-detour` |
| `diagonal-three-routes` | `./tools/run-cases.sh -v diagonal-three-routes` | `./tools/run-case-visual.sh diagonal-three-routes` |
| `horizontal-trunk-vertical-legs` | `./tools/run-cases.sh -v horizontal-trunk-vertical-legs` | `./tools/run-case-visual.sh horizontal-trunk-vertical-legs` |
| `wide-separated-vertical-lanes` | `./tools/run-cases.sh -v wide-separated-vertical-lanes` | `./tools/run-case-visual.sh wide-separated-vertical-lanes` |
| `l-fork-three-branches` | `./tools/run-cases.sh -v l-fork-three-branches` | `./tools/run-case-visual.sh l-fork-three-branches` |
| `dual-fork-top3-assign` | `./tools/run-cases.sh -v dual-fork-top3-assign` | `./tools/run-case-visual.sh dual-fork-top3-assign` |
| `l-turn-parallel-offset` | `./tools/run-cases.sh -v l-turn-parallel-offset` | `./tools/run-case-visual.sh l-turn-parallel-offset` |
| `dense-trunk-perf-scene` | `./tools/run-cases.sh -v dense-trunk-perf-scene` | `./tools/run-case-visual.sh dense-trunk-perf-scene` |

---

## 6. 目录结构

```
RoutesLabel/
├── CMakeLists.txt                顶层构建脚本
├── cmake/
│   └── CompileShaders.cmake      GLSL → SPIR-V 编译辅助
├── tools/
│   ├── run-mac.sh                macOS 运行包装（MoltenVK / Layer / dyld 路径）
│   ├── run-linux.sh              Linux 运行包装（LunarG SDK + validation 开关）
│   ├── run-cases.sh              headless 跑 assets/cases
│   ├── run-case-visual.sh        GUI 加载单个 case
│   └── run-all-cases-visual.sh   列出全部 case 命令
├── third_party/
│   ├── glfw/                     GLFW（submodule）
│   └── glm/                      GLM（submodule）
├── shaders/
│   ├── grid_clip.comp            Stage A：线段 → tile 裁剪
│   ├── grid_pca.comp             Stage B：per-tile 加权 PCA + 闭式特征分解
│   ├── grid_score.comp           Stage C：separation / density / score
│   ├── grid_scan.comp            辅助 prefix scan
│   ├── grid_label.comp           Stage D：候选 label 评分
│   ├── grid_common.glsl          common include
│   ├── routes.{vert,frag}        路径渲染
│   └── triangle.{vert,frag}      hello-triangle 自检
├── src/
│   ├── main.cpp                  入口
│   ├── app/                      Application：组合 Window + Renderer，主循环
│   ├── platform/                 GLFW + Vulkan Surface
│   ├── rhi/                      Vulkan 资源 RAII 封装（Instance / Device / Swapchain / Pipeline / Buffer ...）
│   ├── algo/
│   │   ├── GridCommon.h          共享数据结构（Tile / SubSeg / Score）
│   │   ├── GridCpu.{h,cpp}       CPU 参考实现 / Ground Truth
│   │   └── GridGpu.{h,cpp}       Vulkan compute pipeline 编排（4 stage + readback）
│   ├── renderer/
│   │   ├── MapView.{h,cpp}       屏幕坐标系 / 视口
│   │   ├── RouteScene.{h,cpp}    三路径场景与状态
│   │   ├── RoutesRenderer.{h,cpp} 路径绘制
│   │   └── TriangleRenderer.{h,cpp} hello-triangle 自检
│   ├── debug/                    ImGui overlay：Grid 热图 / PCA 主轴 / separation 直方图
│   ├── tools/RunCases.cpp        headless case runner（routes_label_cases）
│   └── utils/                    Log / FileSystem / SceneAlgo 等
├── assets/cases/                 label 回归 JSON（见 §5.5）
└── doc/                          设计文档
```

---

## 7. 设计原则

- **分层**：`Platform → RHI → Algo / Renderer → App`，单向依赖，便于替换/扩展
- **RAII**：所有 Vulkan handle 由对应类的析构函数销毁，遵循 Vulkan 销毁顺序
- **CPU/GPU 等价**：`GridCpu` 与 `GridGpu` 输出严格一致，互为对照（GPU 调试时直接 diff）
- **跨端零分支**：唯一的平台分支集中在 `rhi/Instance.cpp`（macOS portability flag），App 层无任何 `#ifdef`
- **算法即文档**：`doc/` 中每条公式、每个常量都对应到 `src/algo/` 中具名变量，便于交叉阅读

---

## 8. Roadmap

- [x] CPU Grid PCA Separation 参考实现
- [x] Vulkan compute shader 4-stage pipeline 等价移植
- [x] ImGui Debug Overlay（Grid 热图 / PCA 主轴 / separation 直方图）
- [ ] Stage A 的 scan-scatter 改造（消除 atomic 竞争）
- [ ] Stage D 纯 GPU bitonic Top-3
- [ ] fp16 协方差累加（移动 GPU 优化）
- [ ] Indirect dispatch 跳过空 tile
- [ ] WebGPU port（让 Demo 在浏览器跑起来）
- [ ] 接入真实矢量瓦片地图后端（替换示例三路径数据）

---

## 9. License

见 [LICENSE](LICENSE)。

---

## 10. 引用

如果本算法或实现对你的工作有帮助，欢迎引用本仓库

> Issue / PR welcome —— 特别是 GPU 优化方向（scan-scatter / fp16 / indirect dispatch / 纯 GPU Top-K），以及在不同移动 GPU 上的 perf 数据贡献。
