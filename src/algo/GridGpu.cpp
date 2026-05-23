//
// algo/GridGpu.cpp
// GPU 实现（Vulkan compute shader 4-stage pipeline）。
// 对应 doc/routes-select-grid-gpu.md §4 / §5 / §6。
//
// 当前阶段（骨架）：完成构造期 Vulkan 资源准备 + descriptor 配置 + 5 个 ComputePipeline
// 加载；compute() 内部根据 is_available() 决定走完整 dispatch 路径还是直接降级。
// 完整 dispatch 实现见后续 todo（grid-gpu-dispatch）。
//

#include "algo/GridGpu.h"

#include "rhi/Buffer.h"
#include "rhi/ComputePipeline.h"
#include "rhi/Descriptor.h"
#include "rhi/Device.h"
#include "rhi/PhysicalDevice.h"
#include "utils/FileSystem.h"
#include "utils/Log.h"

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace routes_label::algo {

namespace {

// -----------------------------------------------------------------------------
// 静态预算（doc §2.2 容量预算的工程上限版）
// -----------------------------------------------------------------------------
// n_grid 滑块上限 80 → 1080×1080 / (13.5²) ≈ 6400 tile，留 25% 余量取 8192。
constexpr uint32_t kMaxTiles    = 8192;
// 三路径 polyline，扩容到 ~5K 顶点 demo（每条 ~1700 点） + 额外余量。
// 每段 voxel-traverse 平均切 1~3 个 sub_seg → 5K×~3 = 15K 上限，2× 余量取 32768。
constexpr uint32_t kMaxSubsegs  = 32768;
// 线段 AABB 入箱：5K 段、平均 1~2 tile，~10K，余量取 16384。
constexpr uint32_t kMaxAabbs    = 16384;
// 单一 polyline 顶点数上限（×3 路径），扩到 16384 容纳 ~5K 顶点 demo + 余量。
constexpr uint32_t kMaxPolyVerts = 16384;

// -----------------------------------------------------------------------------
// std430 SSBO 布局（与 5 个 .comp shader 中的 struct 字面相同）
// -----------------------------------------------------------------------------
//
// 这些"GPU 端字面"结构与 GridCommon.h 中的 CPU-friendly 结构是**同义但布局不同**：
//   - GridCommon::SubSeg     ≠ Gpu::SubSeg（GPU 端 padding 到 32B）
//   - GridCommon::SegAABB    ≠ Gpu::SegAABB
//   - GridCommon::TilePca    ≠ Gpu::TilePca
//   - GridCommon::TileScore  ≠ Gpu::TileScore
//   - GridCommon::LabelResult ≠ Gpu::LabelResult（doc §6.2 32B std430）
//
// CPU↔GPU 边界负责 marshalling（compute() 内）。本头只用于声明 buffer 的字节预算。

constexpr uint32_t kSubsegStride    = 32;   // vec2(8) + vec2(8) + float(4) + uint(4) + pad(8) = 32
constexpr uint32_t kAabbStride      = 32;   // vec2(8) + vec2(8) + uint(4) + uint(4) + pad(8) = 32
constexpr uint32_t kTilePcaStride   = 64;   // vec2 mu(8) + vec2 axis_u(8) + vec2 axis_v(8) + 2 float(8) + 3 float arclength(12) + uint valid(4) + pad(16) = 64
constexpr uint32_t kTileScoreStride = 64;   // float separation(4) + density(4) + score(4) + uint feasible(4) + 3*ProjInterval(3*12=36) + pad(12) = 64
constexpr uint32_t kLabelStride     = 32;   // doc §6.2

// 状态/标志 buffer 大小（小固定）
constexpr uint32_t kStatusFlagsBytes = 16;  // [0]=pool_overflow, [1..3]=保留

// -----------------------------------------------------------------------------
// shader 文件名（与 cmake/CompileShaders.cmake 输出对应）
// -----------------------------------------------------------------------------
constexpr const char* kShaderClip  = "grid_clip.comp.spv";
constexpr const char* kShaderScan  = "grid_scan.comp.spv";
constexpr const char* kShaderPca   = "grid_pca.comp.spv";
constexpr const char* kShaderScore = "grid_score.comp.spv";
constexpr const char* kShaderLabel = "grid_label.comp.spv";

std::filesystem::path shader_path(const char* filename) {
    return utils::executable_dir() / "shaders" / filename;
}

// -----------------------------------------------------------------------------
// Descriptor binding 编号（5 个 shader 共用同一 set layout）
// -----------------------------------------------------------------------------
enum Bind : uint32_t {
    kBindParamsUbo       = 0,
    kBindPolylines       = 1,
    kBindSegmentOffsets  = 2,
    kBindSubsegCount     = 3,
    kBindSubsegOffset    = 4,
    kBindSubsegPool      = 5,
    kBindAabbCount       = 6,
    kBindAabbOffset      = 7,
    kBindAabbPool        = 8,
    kBindTilePca         = 9,
    kBindTileScore       = 10,
    kBindFinalLabels     = 11,
    kBindStatusFlags     = 12,
    kBindCount           = 13,
};

}  // namespace

// -----------------------------------------------------------------------------
// GridGpu::Impl —— 隐藏 Vulkan 资源
// -----------------------------------------------------------------------------

struct GridGpu::Impl {
    rhi::Device&         device;
    rhi::PhysicalDevice& physical;

    bool        available = false;
    std::string last_error;

    // SSBO buffers（device-local）
    rhi::Buffer params_ubo;          // VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT host-visible
    rhi::Buffer polylines_buf;       // host-visible（每次 compute 上传）
    rhi::Buffer segment_offsets_buf; // host-visible（小，每次 compute 上传）
    rhi::Buffer subseg_count_buf;
    rhi::Buffer subseg_offset_buf;
    rhi::Buffer subseg_pool_buf;
    rhi::Buffer aabb_count_buf;
    rhi::Buffer aabb_offset_buf;
    rhi::Buffer aabb_pool_buf;
    rhi::Buffer tile_pca_buf;
    rhi::Buffer tile_score_buf;
    rhi::Buffer final_labels_buf;
    rhi::Buffer status_flags_buf;

    // Readback staging buffers（host-visible）
    rhi::Buffer final_labels_staging;
    rhi::Buffer status_flags_staging;
    rhi::Buffer tile_pca_staging;        // 仅 collect_debug=true 时使用
    rhi::Buffer tile_score_staging;      // 仅 collect_debug=true 时使用

    // Descriptor
    std::unique_ptr<rhi::DescriptorSetLayout> set_layout;
    std::unique_ptr<rhi::DescriptorPool>      desc_pool;
    std::unique_ptr<rhi::DescriptorSets>      desc_sets;

    // 5 个 ComputePipeline
    std::unique_ptr<rhi::ComputePipeline> pipe_clip;
    std::unique_ptr<rhi::ComputePipeline> pipe_scan;
    std::unique_ptr<rhi::ComputePipeline> pipe_pca;
    std::unique_ptr<rhi::ComputePipeline> pipe_score;
    std::unique_ptr<rhi::ComputePipeline> pipe_label;

    // 专用 transient command pool（与 graphics queue 共享 family，简化同步）
    VkCommandPool   cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd_buf  = VK_NULL_HANDLE;
    VkFence         fence    = VK_NULL_HANDLE;

    // GPU timestamp（doc / plan grid-perf-A-B：分两段精测 GPU work vs Round-trip）
    //   slot 0 = host->GPU memory barrier 之后、fill 之前   → round-trip 起点
    //   slot 1 = 第一个 dispatch (pipe_scan) 之前           → GPU work 起点
    //   slot 2 = 最后一个 dispatch (pipe_label) 之后        → GPU work 终点
    //   slot 3 = host-readable barrier 之后、cmd_buf 末尾   → round-trip 终点
    VkQueryPool query_pool          = VK_NULL_HANDLE;
    bool        timestamps_supported = false;
    double      ts_period_ns        = 0.0;   // physical.properties.limits.timestampPeriod

    Impl(rhi::Device& d, rhi::PhysicalDevice& p) : device(d), physical(p) {}
    ~Impl() { destroy_vk_objects(); }

    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;

    void destroy_vk_objects() noexcept {
        VkDevice dev = device.handle();
        if (query_pool != VK_NULL_HANDLE) { vkDestroyQueryPool(dev, query_pool, nullptr); query_pool = VK_NULL_HANDLE; }
        if (fence    != VK_NULL_HANDLE) { vkDestroyFence(dev, fence, nullptr);            fence    = VK_NULL_HANDLE; }
        if (cmd_pool != VK_NULL_HANDLE) { vkDestroyCommandPool(dev, cmd_pool, nullptr);   cmd_pool = VK_NULL_HANDLE; cmd_buf = VK_NULL_HANDLE; }
        // pipeline / desc / buffer 由 unique_ptr / Buffer RAII 自动释放（顺序无关）
    }

    bool init() {
        try {
            allocate_buffers();
            create_descriptor_layout_and_set();
            update_descriptor_set();
            create_pipelines();
            create_cmd_pool_and_fence();
            create_query_pool();   // 能力探测 + 创建 4-slot timestamp query pool（失败不致命）
            available = true;
            LOG_INFO("[GridGpu] initialized successfully (compute pipeline ready)");
            return true;
        } catch (const std::exception& e) {
            available = false;
            last_error = e.what();
            LOG_WARN("[GridGpu] initialization failed, will fallback to CPU: " << e.what());
            // 已经分配的资源走 RAII 析构，无需手动清理
            return false;
        }
    }

    void allocate_buffers() {
        // UBO（params） / polylines / segment_offsets：host-visible（每次重算上传）
        // Params UBO 与 grid_common.glsl 的 Params block 对齐，96B 含 padding。
        params_ubo = rhi::Buffer::CreateHostVisible(
            device, physical, /*Params std140*/ 96,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        polylines_buf = rhi::Buffer::CreateHostVisible(
            device, physical, kMaxPolyVerts * 8u,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        segment_offsets_buf = rhi::Buffer::CreateHostVisible(
            device, physical, /*8 uint = 32B*/ 32,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        // 中间 buffer：device-local SSBO（GPU 内部传递）
        auto make_ssbo = [&](VkDeviceSize bytes) {
            return rhi::Buffer::CreateDeviceLocal(
                device, physical, bytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                    | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                /*src=*/nullptr);
        };
        subseg_count_buf  = make_ssbo(kMaxTiles * 3u * sizeof(uint32_t));
        subseg_offset_buf = make_ssbo(kMaxTiles * 3u * sizeof(uint32_t));
        subseg_pool_buf   = make_ssbo(kMaxSubsegs * kSubsegStride);
        aabb_count_buf    = make_ssbo(kMaxTiles * sizeof(uint32_t));
        aabb_offset_buf   = make_ssbo(kMaxTiles * sizeof(uint32_t));
        aabb_pool_buf     = make_ssbo(kMaxAabbs * kAabbStride);
        tile_pca_buf      = make_ssbo(kMaxTiles * kTilePcaStride);
        tile_score_buf    = make_ssbo(kMaxTiles * kTileScoreStride);
        final_labels_buf  = make_ssbo(/*3 labels*/ 3u * kLabelStride);
        status_flags_buf  = make_ssbo(kStatusFlagsBytes);

        // Readback staging
        final_labels_staging = rhi::Buffer::CreateHostVisible(
            device, physical, 3u * kLabelStride,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        status_flags_staging = rhi::Buffer::CreateHostVisible(
            device, physical, kStatusFlagsBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        tile_pca_staging = rhi::Buffer::CreateHostVisible(
            device, physical, kMaxTiles * kTilePcaStride,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        tile_score_staging = rhi::Buffer::CreateHostVisible(
            device, physical, kMaxTiles * kTileScoreStride,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT);

        LOG_INFO("[GridGpu] all SSBO/UBO/staging allocated (kMaxTiles=" << kMaxTiles
                 << ", kMaxSubsegs=" << kMaxSubsegs
                 << ", kMaxAabbs=" << kMaxAabbs << ")");
    }

    void create_descriptor_layout_and_set() {
        std::vector<VkDescriptorSetLayoutBinding> bindings(kBindCount);
        auto add = [&](uint32_t b, VkDescriptorType t) {
            bindings[b].binding         = b;
            bindings[b].descriptorType  = t;
            bindings[b].descriptorCount = 1;
            bindings[b].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        };
        add(kBindParamsUbo,      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        add(kBindPolylines,      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindSegmentOffsets, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindSubsegCount,    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindSubsegOffset,   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindSubsegPool,     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindAabbCount,      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindAabbOffset,     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindAabbPool,       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindTilePca,        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindTileScore,      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindFinalLabels,    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(kBindStatusFlags,    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        set_layout = std::make_unique<rhi::DescriptorSetLayout>(device, bindings);

        std::vector<VkDescriptorPoolSize> pool_sizes = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kBindCount - 1 },
        };
        desc_pool = std::make_unique<rhi::DescriptorPool>(device, /*max_sets=*/1, pool_sizes);

        desc_sets = std::make_unique<rhi::DescriptorSets>(device, *desc_pool, *set_layout, /*count=*/1);
    }

    void update_descriptor_set() {
        // 0=ubo, 1..12=ssbo
        desc_sets->update_uniform_buffer(0, kBindParamsUbo,
            params_ubo.handle(), 0, params_ubo.size());

        auto bind_ssbo = [&](uint32_t b, const rhi::Buffer& buf) {
            desc_sets->update_storage_buffer(0, b, buf.handle(), 0, buf.size());
        };
        bind_ssbo(kBindPolylines,      polylines_buf);
        bind_ssbo(kBindSegmentOffsets, segment_offsets_buf);
        bind_ssbo(kBindSubsegCount,    subseg_count_buf);
        bind_ssbo(kBindSubsegOffset,   subseg_offset_buf);
        bind_ssbo(kBindSubsegPool,     subseg_pool_buf);
        bind_ssbo(kBindAabbCount,      aabb_count_buf);
        bind_ssbo(kBindAabbOffset,     aabb_offset_buf);
        bind_ssbo(kBindAabbPool,       aabb_pool_buf);
        bind_ssbo(kBindTilePca,        tile_pca_buf);
        bind_ssbo(kBindTileScore,      tile_score_buf);
        bind_ssbo(kBindFinalLabels,    final_labels_buf);
        bind_ssbo(kBindStatusFlags,    status_flags_buf);
    }

    void create_pipelines() {
        std::vector<VkDescriptorSetLayout> set_layouts = { set_layout->handle() };

        auto make_pipe = [&](const char* spv_name) {
            rhi::ComputePipelineDesc desc;
            desc.comp_spv = shader_path(spv_name);
            desc.descriptor_set_layouts = set_layouts;
            desc.push_constant_size     = 0;  // 所有参数走 UBO，不用 push_constant
            return std::make_unique<rhi::ComputePipeline>(device, desc);
        };
        pipe_clip  = make_pipe(kShaderClip);
        pipe_scan  = make_pipe(kShaderScan);
        pipe_pca   = make_pipe(kShaderPca);
        pipe_score = make_pipe(kShaderScore);
        pipe_label = make_pipe(kShaderLabel);
    }

    void create_cmd_pool_and_fence() {
        VkCommandPoolCreateInfo pci = {};
        pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = device.graphics_family();
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(device.handle(), &pci, nullptr, &cmd_pool));

        VkCommandBufferAllocateInfo ai = {};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = cmd_pool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device.handle(), &ai, &cmd_buf));

        VkFenceCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VK_CHECK(vkCreateFence(device.handle(), &fci, nullptr, &fence));
    }

    // 创建 4-slot timestamp query pool（不致命：失败时 timestamps_supported=false，
    // compute() 会跳过写读，把 GridResult.gpu_*_ms 留 -1 表示 N/A）。
    void create_query_pool() noexcept {
        timestamps_supported = false;
        ts_period_ns         = static_cast<double>(physical.properties().limits.timestampPeriod);
        if (ts_period_ns <= 0.0) {
            LOG_WARN("[GridGpu] timestampPeriod=0 → GPU timestamp disabled");
            return;
        }

        // 查 graphics_family 的 timestampValidBits：必须 >0 才能在该 queue 上写 timestamp
        uint32_t qfam_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical.handle(), &qfam_count, nullptr);
        if (qfam_count == 0) {
            LOG_WARN("[GridGpu] no queue families → GPU timestamp disabled");
            return;
        }
        std::vector<VkQueueFamilyProperties> qprops(qfam_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical.handle(), &qfam_count, qprops.data());
        const uint32_t gfx = device.graphics_family();
        if (gfx >= qfam_count || qprops[gfx].timestampValidBits == 0) {
            LOG_WARN("[GridGpu] graphics queue family timestampValidBits=0 → GPU timestamp disabled");
            return;
        }

        VkQueryPoolCreateInfo qpci = {};
        qpci.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qpci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        qpci.queryCount = 4;   // slot 0..3
        VkResult vr = vkCreateQueryPool(device.handle(), &qpci, nullptr, &query_pool);
        if (vr != VK_SUCCESS || query_pool == VK_NULL_HANDLE) {
            LOG_WARN("[GridGpu] vkCreateQueryPool failed (" << vr << ") → GPU timestamp disabled");
            query_pool = VK_NULL_HANDLE;
            return;
        }
        timestamps_supported = true;
        LOG_INFO("[GridGpu] timestamp query pool ready (period=" << ts_period_ns << " ns/tick)");
    }
};

// -----------------------------------------------------------------------------
// GridGpu 公开接口
// -----------------------------------------------------------------------------

GridGpu::GridGpu(rhi::Device& device, rhi::PhysicalDevice& physical)
    : impl_(std::make_unique<Impl>(device, physical)) {
    impl_->init();
}

GridGpu::~GridGpu() = default;

bool GridGpu::is_available() const noexcept {
    return impl_ && impl_->available;
}

const std::string& GridGpu::last_error() const noexcept {
    static const std::string kEmpty;
    return impl_ ? impl_->last_error : kEmpty;
}

GridResult GridGpu::compute(const Polylines& polylines,
                            const GridParams& params,
                            bool collect_debug) {
    GridResult out{};
    if (!is_available()) {
        for (int r = 0; r < kRouteCount; ++r) {
            out.labels[r].route_id = r;
            out.labels[r].status   = LabelStatus::Infeasible;
        }
        return out;
    }

    const auto t0 = std::chrono::steady_clock::now();

    Impl& I = *impl_;
    VkDevice dev = I.device.handle();

    // -------------------------------------------------------------------------
    // 0. 计算派生几何（与 GridCpu::compute 同步）
    // -------------------------------------------------------------------------
    if (params.screen_w <= 0.0f || params.screen_h <= 0.0f || params.n_grid <= 1) {
        for (int r = 0; r < kRouteCount; ++r) {
            out.labels[r].route_id = r;
            out.labels[r].status   = LabelStatus::Infeasible;
        }
        return out;
    }
    const float tile_size = std::max(1.0f, std::floor(
        std::min(params.screen_w, params.screen_h) /
        static_cast<float>(params.n_grid)));
    const int   n_x = std::max(1, static_cast<int>(std::ceil(params.screen_w / tile_size)));
    const int   n_y = std::max(1, static_cast<int>(std::ceil(params.screen_h / tile_size)));
    const uint32_t n_tile = static_cast<uint32_t>(n_x) * static_cast<uint32_t>(n_y);
    if (n_tile > kMaxTiles) {
        LOG_WARN("[GridGpu] n_tile=" << n_tile << " exceeds kMaxTiles=" << kMaxTiles
                 << ", clipping to fallback");
        for (int r = 0; r < kRouteCount; ++r) {
            out.labels[r].route_id = r;
            out.labels[r].status   = LabelStatus::PoolOverflow;
        }
        I.last_error = "n_tile exceeds kMaxTiles";
        return out;
    }

    // -------------------------------------------------------------------------
    // 1. 上传 polylines + segment_offsets + Params UBO
    // -------------------------------------------------------------------------
    // 1a. polyline 顶点：紧凑布局成 [route0..; route1..; route2..]
    std::array<uint32_t, kRouteCount> route_off  = { 0, 0, 0 };
    std::array<uint32_t, kRouteCount> route_len  = { 0, 0, 0 };
    {
        uint32_t cumulative = 0;
        for (int r = 0; r < kRouteCount; ++r) {
            const auto& poly = polylines.routes[r];
            route_off[r] = cumulative;
            route_len[r] = static_cast<uint32_t>(poly.size());
            cumulative += route_len[r];
        }
        if (cumulative > kMaxPolyVerts) {
            LOG_WARN("[GridGpu] poly verts=" << cumulative
                     << " exceeds kMaxPolyVerts=" << kMaxPolyVerts);
            for (int r = 0; r < kRouteCount; ++r) {
                out.labels[r].route_id = r;
                out.labels[r].status   = LabelStatus::PoolOverflow;
            }
            return out;
        }
    }

    // 拷贝到 host-visible buffer（每个顶点 8B vec2，与 Vec2Pad 对齐一致）
    {
        auto* dst = static_cast<float*>(I.polylines_buf.mapped());
        for (int r = 0; r < kRouteCount; ++r) {
            for (size_t k = 0; k < polylines.routes[r].size(); ++k) {
                dst[(route_off[r] + k) * 2 + 0] = polylines.routes[r][k].x;
                dst[(route_off[r] + k) * 2 + 1] = polylines.routes[r][k].y;
            }
        }
    }

    // 1b. segment_offsets uint[8]
    {
        auto* dst = static_cast<uint32_t*>(I.segment_offsets_buf.mapped());
        dst[0] = route_off[0];
        dst[1] = route_off[1];
        dst[2] = route_off[2];
        dst[3] = route_len[0];
        dst[4] = route_len[1];
        dst[5] = route_len[2];
        dst[6] = 0;
        dst[7] = 0;
    }

    // 1c. Params UBO（与 grid_common.glsl 中 Params block 字段顺序对齐）
    {
        struct ParamsUbo {
            float screen_w; float screen_h; int n_x; int n_y;
            float tile_size; int n_grid; float separationThreshold; float arclength_balance;
            int nms_grid_distance; float anisotropy_min; float label_w; float label_h;
            float leader_len; int total_poly_verts; float panel_min_x; float panel_min_y;
            float panel_max_x; float panel_max_y; float pad0; float pad1;
        };
        static_assert(sizeof(ParamsUbo) == 80, "Params UBO size must match GLSL");

        ParamsUbo u = {};
        u.screen_w            = params.screen_w;
        u.screen_h            = params.screen_h;
        u.n_x                 = n_x;
        u.n_y                 = n_y;
        u.tile_size           = tile_size;
        u.n_grid              = params.n_grid;
        u.separationThreshold = params.separationThreshold;
        u.arclength_balance   = params.arclength_balance;
        u.nms_grid_distance   = params.nms_grid_distance;
        u.anisotropy_min      = params.anisotropy_min;
        u.label_w             = params.label_w;
        u.label_h             = params.label_h;
        u.leader_len          = params.leader_len;
        u.total_poly_verts    = static_cast<int>(route_off[2] + route_len[2]);
        u.panel_min_x         = params.panel_rect.empty() ? 0.0f : params.panel_rect.mn.x;
        u.panel_min_y         = params.panel_rect.empty() ? 0.0f : params.panel_rect.mn.y;
        u.panel_max_x         = params.panel_rect.empty() ? 0.0f : params.panel_rect.mx.x;
        u.panel_max_y         = params.panel_rect.empty() ? 0.0f : params.panel_rect.mx.y;
        u.pad0 = u.pad1 = 0.0f;

        std::memcpy(I.params_ubo.mapped(), &u, sizeof(u));
    }

    // -------------------------------------------------------------------------
    // 2. 录制 command buffer：5 dispatch + barriers + copy to staging
    // -------------------------------------------------------------------------
    VK_CHECK(vkResetCommandBuffer(I.cmd_buf, 0));

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(I.cmd_buf, &bi));

    // GPU timestamp（必须在录制状态下 reset；首次/复用都安全）
    if (I.timestamps_supported) {
        vkCmdResetQueryPool(I.cmd_buf, I.query_pool, 0, 4);
    }

    // host-visible writes（polylines_buf / segment_offsets_buf / params_ubo）→ shader read
    // 显式 barrier 让 GPU 看到 host 端 memcpy 的内容。
    {
        VkMemoryBarrier mb = {};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
        vkCmdPipelineBarrier(I.cmd_buf,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    // [Timestamp slot 0] round-trip 起点（host->GPU barrier 之后、fill 之前）
    if (I.timestamps_supported) {
        vkCmdWriteTimestamp(I.cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, I.query_pool, 0);
    }

    // 显式 zero-init 关键计数 buffer：避免依赖 grid_scan 的清零（race-free 路径）
    vkCmdFillBuffer(I.cmd_buf, I.status_flags_buf.handle(),  0, VK_WHOLE_SIZE, 0u);
    vkCmdFillBuffer(I.cmd_buf, I.subseg_count_buf.handle(),  0, VK_WHOLE_SIZE, 0u);
    vkCmdFillBuffer(I.cmd_buf, I.aabb_count_buf.handle(),    0, VK_WHOLE_SIZE, 0u);
    {
        VkMemoryBarrier mb = {};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(I.cmd_buf,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    // 计算总 segment 数（dispatch_x for stage A）
    uint32_t total_segs = 0;
    for (int r = 0; r < kRouteCount; ++r) {
        if (route_len[r] >= 2) total_segs += route_len[r] - 1;
    }
    auto ceil_div = [](uint32_t n, uint32_t d) -> uint32_t {
        return (n + d - 1) / d;
    };

    VkDescriptorSet dset = I.desc_sets->at(0);
    auto bind_pipe = [&](rhi::ComputePipeline& p) {
        vkCmdBindPipeline(I.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, p.handle());
        vkCmdBindDescriptorSets(I.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                p.layout(), 0, 1, &dset, 0, nullptr);
    };
    auto barrier_compute = [&]() {
        VkMemoryBarrier mb = {};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(I.cmd_buf,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    };

    // ---- Stage 0: scan/zero-init ----
    // [Timestamp slot 1] GPU work 起点（fill+barrier 之后、第一个 dispatch 之前）
    if (I.timestamps_supported) {
        vkCmdWriteTimestamp(I.cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, I.query_pool, 1);
    }
    bind_pipe(*I.pipe_scan);
    vkCmdDispatch(I.cmd_buf, ceil_div(kMaxTiles, 64), 1, 1);
    barrier_compute();

    // ---- Stage A: clip + AABB 入箱 ----
    if (total_segs > 0) {
        bind_pipe(*I.pipe_clip);
        vkCmdDispatch(I.cmd_buf, ceil_div(total_segs, 64), 1, 1);
        barrier_compute();
    }

    // ---- Stage B: per-tile PCA ----
    bind_pipe(*I.pipe_pca);
    vkCmdDispatch(I.cmd_buf, n_tile, 1, 1);
    barrier_compute();

    // ---- Stage C: per-tile score ----
    bind_pipe(*I.pipe_score);
    vkCmdDispatch(I.cmd_buf, n_tile, 1, 1);
    barrier_compute();

    // ---- Stage D: Top-3 + label 摆放 ----
    bind_pipe(*I.pipe_label);
    vkCmdDispatch(I.cmd_buf, 1, 1, 1);
    barrier_compute();

    // [Timestamp slot 2] GPU work 终点（最后一个 dispatch + barrier 完成后、staging copy 之前）
    if (I.timestamps_supported) {
        vkCmdWriteTimestamp(I.cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, I.query_pool, 2);
    }

    // ---- Copy SSBO → host-visible staging ----
    auto copy = [&](rhi::Buffer& src, rhi::Buffer& dst, VkDeviceSize size) {
        VkBufferCopy bc = {};
        bc.size = size;
        vkCmdCopyBuffer(I.cmd_buf, src.handle(), dst.handle(), 1, &bc);
    };
    copy(I.final_labels_buf, I.final_labels_staging, 3u * kLabelStride);
    copy(I.status_flags_buf, I.status_flags_staging, kStatusFlagsBytes);
    if (collect_debug) {
        copy(I.tile_pca_buf,   I.tile_pca_staging,   static_cast<VkDeviceSize>(n_tile) * kTilePcaStride);
        copy(I.tile_score_buf, I.tile_score_staging, static_cast<VkDeviceSize>(n_tile) * kTileScoreStride);
    }

    // host-readable barrier
    {
        VkMemoryBarrier mb = {};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(I.cmd_buf,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    // [Timestamp slot 3] round-trip 终点（host-readable barrier 之后、cmd_buf 末尾）
    if (I.timestamps_supported) {
        vkCmdWriteTimestamp(I.cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, I.query_pool, 3);
    }

    VK_CHECK(vkEndCommandBuffer(I.cmd_buf));

    // -------------------------------------------------------------------------
    // 3. Submit + wait fence
    // -------------------------------------------------------------------------
    VK_CHECK(vkResetFences(dev, 1, &I.fence));
    VkSubmitInfo si = {};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &I.cmd_buf;
    VK_CHECK(vkQueueSubmit(I.device.graphics_queue(), 1, &si, I.fence));

    constexpr uint64_t kFenceTimeoutNs = 1'000'000'000ull;   // 1 s
    VkResult fr = vkWaitForFences(dev, 1, &I.fence, VK_TRUE, kFenceTimeoutNs);
    if (fr != VK_SUCCESS) {
        I.last_error = "vkWaitForFences timeout";
        LOG_ERROR("[GridGpu] dispatch timeout/fence error: " << fr);
        for (int r = 0; r < kRouteCount; ++r) {
            out.labels[r].route_id = r;
            out.labels[r].status   = LabelStatus::Infeasible;
        }
        return out;
    }

    // -------------------------------------------------------------------------
    // 3.1 读取 GPU timestamp（fence 已 wait，无需 WAIT_BIT）
    //   slot 0 → round-trip 起点；slot 3 → round-trip 终点
    //   slot 1 → GPU work 起点；   slot 2 → GPU work 终点
    // 不支持 timestamp 时保持 -1（GridResult 默认值）。
    // -------------------------------------------------------------------------
    if (I.timestamps_supported) {
        uint64_t ticks[4] = { 0, 0, 0, 0 };
        VkResult qr = vkGetQueryPoolResults(
            dev, I.query_pool, 0, 4,
            sizeof(ticks), ticks, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (qr == VK_SUCCESS) {
            // 防御：tick 应单调递增；个别驱动若返回乱序则按 0 处理
            const uint64_t t0 = ticks[0];
            const uint64_t t1 = ticks[1];
            const uint64_t t2 = ticks[2];
            const uint64_t t3 = ticks[3];
            const double work_ns       = (t2 > t1) ? static_cast<double>(t2 - t1) * I.ts_period_ns : 0.0;
            const double round_trip_ns = (t3 > t0) ? static_cast<double>(t3 - t0) * I.ts_period_ns : 0.0;
            out.gpu_work_ms       = static_cast<float>(work_ns       * 1e-6);
            out.gpu_round_trip_ms = static_cast<float>(round_trip_ns * 1e-6);
        } else {
            // VK_NOT_READY 之类异常一律降级为 N/A，不阻塞主流程
            out.gpu_work_ms       = -1.0f;
            out.gpu_round_trip_ms = -1.0f;
        }
    }

    // -------------------------------------------------------------------------
    // 4. 解码 readback：final_labels + status_flags + (optional) debug
    // -------------------------------------------------------------------------
    // status_flags
    uint32_t status[4] = { 0, 0, 0, 0 };
    std::memcpy(status, I.status_flags_staging.mapped(), sizeof(status));
    if (status[0] != 0) {
        LOG_WARN("[GridGpu] pool overflow detected (subseg_count=" << status[1]
                 << ", aabb_count=" << status[2] << "); falling back");
        I.last_error = "pool overflow";
        for (int r = 0; r < kRouteCount; ++r) {
            out.labels[r].route_id = r;
            out.labels[r].status   = LabelStatus::PoolOverflow;
        }
        return out;
    }

    // final_labels —— 与 GLSL std430 LabelResult 对齐：vec2 + vec2 + uint + uint + uint + pad = 32B
    {
        struct GpuLabel {
            float anchor_x; float anchor_y;
            float center_x; float center_y;
            uint32_t route_id;
            uint32_t slot;
            uint32_t status;
            uint32_t _pad0;
        };
        static_assert(sizeof(GpuLabel) == 32, "");
        const GpuLabel* gl = static_cast<const GpuLabel*>(I.final_labels_staging.mapped());
        for (int r = 0; r < kRouteCount; ++r) {
            out.labels[r].anchor       = { gl[r].anchor_x, gl[r].anchor_y };
            out.labels[r].label_center = { gl[r].center_x, gl[r].center_y };
            out.labels[r].route_id     = static_cast<int>(gl[r].route_id);
            out.labels[r].slot         = static_cast<LabelSlot>(gl[r].slot);
            out.labels[r].status       = static_cast<LabelStatus>(gl[r].status);
        }
    }

    // -------------------------------------------------------------------------
    // 5. Debug snapshot 解码（可选）
    // -------------------------------------------------------------------------
    if (collect_debug) {
        out.debug.params    = params;
        out.debug.n_x       = n_x;
        out.debug.n_y       = n_y;
        out.debug.tile_size = tile_size;

        // tile_pca
        struct GpuTilePca {
            float mu_x, mu_y;
            float u_x,  u_y;
            float v_x,  v_y;
            float lambda1, lambda2;
            float arclen0, arclen1, arclen2;
            uint32_t valid;
            uint32_t pad0, pad1, pad2, pad3;
        };
        static_assert(sizeof(GpuTilePca) == 64, "");
        out.debug.tile_pca.resize(n_tile);
        const GpuTilePca* gp = static_cast<const GpuTilePca*>(I.tile_pca_staging.mapped());
        for (uint32_t t = 0; t < n_tile; ++t) {
            TilePca& p = out.debug.tile_pca[t];
            p.mu        = { gp[t].mu_x, gp[t].mu_y };
            p.axis_u    = { gp[t].u_x,  gp[t].u_y  };
            p.axis_v    = { gp[t].v_x,  gp[t].v_y  };
            p.lambda1   = gp[t].lambda1;
            p.lambda2   = gp[t].lambda2;
            p.arclength[0] = gp[t].arclen0;
            p.arclength[1] = gp[t].arclen1;
            p.arclength[2] = gp[t].arclen2;
            p.valid     = (gp[t].valid != 0);
        }

        // tile_score
        struct GpuTileScore {
            float separation, density, score;
            uint32_t feasible;
            float iv0_min, iv0_max; uint32_t iv0_valid;
            float iv1_min, iv1_max; uint32_t iv1_valid;
            float iv2_min, iv2_max; uint32_t iv2_valid;
            uint32_t pad0, pad1, pad2;
        };
        static_assert(sizeof(GpuTileScore) == 64, "");
        out.debug.tile_scores.resize(n_tile);
        const GpuTileScore* gs = static_cast<const GpuTileScore*>(I.tile_score_staging.mapped());
        for (uint32_t t = 0; t < n_tile; ++t) {
            TileScore& s = out.debug.tile_scores[t];
            s.separationScore = gs[t].separation;
            s.density         = gs[t].density;
            s.score           = gs[t].score;
            s.feasible        = gs[t].feasible != 0;
            s.intervals[0].t_min = gs[t].iv0_min;
            s.intervals[0].t_max = gs[t].iv0_max;
            s.intervals[0].valid = gs[t].iv0_valid != 0;
            s.intervals[1].t_min = gs[t].iv1_min;
            s.intervals[1].t_max = gs[t].iv1_max;
            s.intervals[1].valid = gs[t].iv1_valid != 0;
            s.intervals[2].t_min = gs[t].iv2_min;
            s.intervals[2].t_max = gs[t].iv2_max;
            s.intervals[2].valid = gs[t].iv2_valid != 0;
        }

        // selected_tile_index：从 final_labels.anchor 反查最近 tile（或在 host 端按 score 重新选 Top-3）
        // 这里用最简单的：按 tile_score.score 全局排序后取 Top-3（与 GPU stage D 选的可能略有差异，
        // 但仅用于 debug 高亮，不影响算法主流程）。
        std::array<int, kRouteCount> selected = { -1, -1, -1 };
        std::array<float, kRouteCount> sel_score = { -1.0f, -1.0f, -1.0f };
        for (int t = 0; t < static_cast<int>(n_tile); ++t) {
            if (!out.debug.tile_scores[t].feasible) continue;
            float sc = out.debug.tile_scores[t].score;
            for (int k = 0; k < kRouteCount; ++k) {
                if (sc > sel_score[k]) {
                    for (int m = kRouteCount - 1; m > k; --m) {
                        sel_score[m] = sel_score[m-1];
                        selected [m] = selected [m-1];
                    }
                    sel_score[k] = sc;
                    selected [k] = t;
                    break;
                }
            }
        }
        out.debug.selected_tile_index = selected;
    }

    const auto t1 = std::chrono::steady_clock::now();
    out.compute_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

    int placed = 0, infeasible = 0;
    for (int r = 0; r < kRouteCount; ++r) {
        if (out.labels[r].status == LabelStatus::Infeasible) ++infeasible;
        else ++placed;
    }
    LOG_INFO("[GridGpu] computed " << total_segs << " segs, grid=" << n_x << "x" << n_y
             << " (s=" << tile_size << " px), tiles=" << n_tile
             << ", subseg=" << status[1] << ", aabb=" << status[2]
             << ", overflow=" << status[0]
             << ", placed=" << placed << ", infeasible=" << infeasible
             << ", time=" << out.compute_ms << " ms"
             << ", gpu_work=" << out.gpu_work_ms << " ms"
             << ", gpu_rt=" << out.gpu_round_trip_ms << " ms");

    return out;
}

}  // namespace routes_label::algo
