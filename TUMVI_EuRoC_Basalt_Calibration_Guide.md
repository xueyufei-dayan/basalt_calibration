# TUM VI EuRoC 数据集：Basalt 双目–IMU 标定与真值对比

本文记录如何**不使用 ROS bag**，直接以 EuRoC 目录格式运行 Basalt 标定，并和 TUM VI 提供的参考标定进行定量比较。

适用数据集目录：

```text
/mnt/d/data/calib_gt/dataset-calib-imu1_512_16
```

对应 Windows 路径：

```text
D:\data\calib_gt\dataset-calib-imu1_512_16
```

该目录包含：

```text
mav0/cam0/data/       左相机图像
mav0/cam1/data/       右相机图像
mav0/imu0/data.csv    IMU 数据
mav0/mocap0/data.csv  动捕轨迹真值（本流程不参与标定）
dso/camchain.yaml     相机和相机–IMU标定参考真值
```

> 重要：要进行盲测，标定完成前不要打开或将 `dso/camchain.yaml` 用作初值。

## 1. 环境与路径

建议在 WSL2 Ubuntu 中运行 Basalt。确认 Basalt 可执行文件位于环境变量中：

```bash
basalt_calibrate --help
basalt_calibrate_imu --help
```

设置路径变量：

```bash
export DATASET=/mnt/d/data/calib_gt/dataset-calib-imu1_512_16
export RESULT=/mnt/d/data/calib_gt/basalt_calib
export APRILGRID=$HOME/.local/etc/basalt/aprilgrid_6x6.json

mkdir -p "$RESULT"
test -f "$APRILGRID"
```

`aprilgrid_6x6.json` 是 Basalt 安装附带的 AprilGrid 配置。应使用与 TUM VI 标定序列匹配的网格配置；不要自行猜测标签尺寸。

## 2. 双目相机标定

TUM VI 参考标定使用等距鱼眼模型。为了可以逐项比较畸变参数，使用 Basalt 的 `kb4` 相机模型：

```bash
basalt_calibrate \
  --dataset-path "$DATASET" \
  --dataset-type euroc \
  --result-path "$RESULT" \
  --aprilgrid "$APRILGRID" \
  --cam-types kb4 kb4
```

Basalt 会打开图形界面。按以下顺序操作：

1. `load_dataset`
2. `detect_corners`
3. `init_intrinsics`
4. `init_extrinsics`
5. `init_opt`
6. 勾选 `opt_until_converge`
7. 点击 `optimize`，等待优化收敛
8. `save_calib`

此步骤会将检测结果和初始位姿缓存到结果目录，并生成/更新：

```text
$RESULT/calibration.json
```

检查要点：

- 两个相机均有充分的 AprilTag 角点检测；
- 检测点覆盖图像中央和边缘；
- 重投影残差没有明显长尾；
- 估计出的双目基线接近 0.101 m。

## 3. 相机–IMU 联合标定

使用同一个结果目录，这样 Basalt 会读取上一步的相机内参和双目外参：

```bash
basalt_calibrate_imu \
  --dataset-path "$DATASET" \
  --dataset-type euroc \
  --result-path "$RESULT" \
  --aprilgrid "$APRILGRID" \
  --gyro-noise-std 0.000282 \
  --accel-noise-std 0.016 \
  --gyro-bias-std 0.0001 \
  --accel-bias-std 0.001
```

在 GUI 中依次执行：

1. `load_dataset`
2. `detect_corners`
3. `init_cam_poses`
4. `init_cam_imu`
5. `init_opt`
6. 勾选 `opt_until_converge`
7. 点击 `optimize`，直到收敛
8. 点击 `save_calib`

### 时间偏移实验

先在不估计时间偏移的条件下保存一次 `calibration.json`，例如：

```bash
cp "$RESULT/calibration.json" "$RESULT/calibration_no_time_offset.json"
```

然后在 GUI 中启用 `opt_cam_time_offset`，再次优化至收敛并保存：

```bash
cp "$RESULT/calibration.json" "$RESULT/calibration_with_time_offset.json"
```

TUM VI 的相机与 IMU 为硬件同步，且发布数据时间戳已对齐。因此时间偏移应接近 `0 ms`。

### 本流程中不要启用的选项

不要点击：

```text
init_mocap
opt_mocap
save_mocap_calib
opt_imu_scale
```

理由：

- `mav0/mocap0/data.csv` 是后续验证用的真值；若在标定中启用动捕优化，则不再是独立评估；
- TUM VI 的发布数据已经完成 IMU 轴对齐和尺度修正，不应在此实验中重复估计 IMU 尺度。

## 4. 对比参考真值

标定完成后，使用对比工具：

```bash
python3 "$RESULT/compare_basalt_tumvi.py" \
  --basalt "$RESULT/calibration.json" \
  --truth "$DATASET/dso/camchain.yaml" \
  --output "$RESULT/comparison.json"
```

若脚本尚未复制到结果目录，可使用其原始位置：

```bash
python3 /mnt/c/Users/27718/Documents/Codex/2026-08-27/wan/outputs/compare_basalt_tumvi.py \
  --basalt "$RESULT/calibration.json" \
  --truth "$DATASET/dso/camchain.yaml" \
  --output "$RESULT/comparison.json"
```

脚本会自动处理坐标系方向：

| 文件 | 变换含义 |
|---|---|
| Basalt `T_imu_cam` | 相机 → IMU |
| TUM VI `T_cam_imu` | IMU → 相机 |

脚本会先求逆并在同一方向下比较，因此输出指标可直接使用：

- `T_cam0_imu` 平移误差（mm）、旋转误差（deg）
- `T_cam1_imu` 平移误差（mm）、旋转误差（deg）
- `T_cam1_cam0` 双目平移/旋转误差
- `fx`、`fy`、`cx`、`cy` 的绝对与相对误差
- KB4/等距鱼眼模型下的畸变参数误差
- 相机–IMU时间偏移（ms）

## 5. 本次实验结果

本次 `comparison.json` 的关键结果如下：

| 指标 | 结果 |
|---|---:|
| cam0 `fx/fy` 相对误差 | 0.366% / 0.359% |
| cam1 `fx/fy` 相对误差 | 0.292% / 0.297% |
| 主点最大绝对误差 | 0.241 px |
| `T_cam0_imu` 平移 / 旋转误差 | 2.193 mm / 0.0564° |
| `T_cam1_imu` 平移 / 旋转误差 | 1.749 mm / 0.0493° |
| `T_cam1_cam0` 平移 / 旋转误差 | 0.427 mm / 0.0164° |
| 时间偏移 | 0.000 ms |

结论：双目外参非常准确；相机–IMU外参也处于很好的水平。焦距误差小于 `0.4%`，主点误差小于 `0.25 px`。

畸变参数 `k2`、`k3` 的数值差相对较大时，不宜脱离重投影残差单独判断。鱼眼模型的畸变参数通常具有相关性；应结合重投影误差与下游 VIO 轨迹精度共同评估。

## 6. 下游 VIO 验证（推荐）

标定参数接近真值不一定完全代表 VIO 性能最优。建议下载一条具有完整动捕轨迹的 TUM VI `room` 序列，以相同配置分别运行：

1. 本文估计的 `calibration.json`；
2. TUM VI 官方参考标定转换后的 Basalt 标定文件。

比较 ATE RMSE、RPE、成功跟踪时长和是否发生重初始化。VIO 的轨迹应与：

```text
mav0/mocap0/data.csv
```

进行 SE(3) 对齐后比较。视觉惯性系统应使用刚体 SE(3) 对齐；若必须使用带尺度的 Sim(3) 才能得到合理结果，需检查 IMU 单位、重力、外参或尺度初始化。
