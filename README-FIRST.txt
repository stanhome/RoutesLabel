RoutesLabel — test cases 完整包
================================

解压后进入本目录（RoutesLabel/），在终端执行：

  ./tools/run-cases.sh -v
      → headless 跑完全部 12 个 case

  ./tools/run-all-cases-visual.sh
      → 打印下面全部命令（可复制）

首次会自动 cmake -S . -B build 并编译；GUI 需 ROUTES_SCENE（已包含在本包）。

-------------------------------------------------------------------------------
12 个 case（stem = 命令里的名字，三条路线同起点同终点）
-------------------------------------------------------------------------------

1) trunk-fork-rejoin-crowding
   主干分叉再汇合，label 易挤
   ./tools/run-cases.sh -v trunk-fork-rejoin-crowding
   ./tools/run-case-visual.sh trunk-fork-rejoin-crowding

2) three-routes-detour-same-od
   中段各自绕行再汇合
   ./tools/run-cases.sh -v three-routes-detour-same-od
   ./tools/run-case-visual.sh three-routes-detour-same-od

3) vertical-staggered-corridor
   纵向走廊，中段横向错开
   ./tools/run-cases.sh -v vertical-staggered-corridor
   ./tools/run-case-visual.sh vertical-staggered-corridor

4) parallel-horizontal-lanes
   水平平行车道错开
   ./tools/run-cases.sh -v parallel-horizontal-lanes
   ./tools/run-case-visual.sh parallel-horizontal-lanes

5) car-marker-fork-detour
   车标附近三分叉（含 carMarkerRect）
   ./tools/run-cases.sh -v car-marker-fork-detour
   ./tools/run-case-visual.sh car-marker-fork-detour

6) diagonal-three-routes
   三路线对角分散
   ./tools/run-cases.sh -v diagonal-three-routes
   ./tools/run-case-visual.sh diagonal-three-routes

7) horizontal-trunk-vertical-legs
   水平主干 + 上下垂直过渡腿
   ./tools/run-cases.sh -v horizontal-trunk-vertical-legs
   ./tools/run-case-visual.sh horizontal-trunk-vertical-legs

8) wide-separated-vertical-lanes
   中段三条竖向车道拉开
   ./tools/run-cases.sh -v wide-separated-vertical-lanes
   ./tools/run-case-visual.sh wide-separated-vertical-lanes

9) l-fork-three-branches
   L 形三叉
   ./tools/run-cases.sh -v l-fork-three-branches
   ./tools/run-case-visual.sh l-fork-three-branches

10) dual-fork-top3-assign
    两簇竖岔，可走 top3 主路径
    ./tools/run-cases.sh -v dual-fork-top3-assign
    ./tools/run-case-visual.sh dual-fork-top3-assign

11) l-turn-parallel-offset
    直角弯，中段平行错开
    ./tools/run-cases.sh -v l-turn-parallel-offset
    ./tools/run-case-visual.sh l-turn-parallel-offset

12) dense-trunk-perf-scene
    大场景（routes_demo.json，点密、较慢）
    ./tools/run-cases.sh -v dense-trunk-perf-scene
    ./tools/run-case-visual.sh dense-trunk-perf-scene

-------------------------------------------------------------------------------
GUI 成功时日志应含：
  [RoutesRenderer] scene JSON: .../build/bin/assets/cases/_active_case.json

更多说明见 README.md 第 5.5 节。
