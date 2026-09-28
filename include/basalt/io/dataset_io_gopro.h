/**
BSD 3-Clause License

This file is part of the Basalt project.
*/

#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <basalt/io/dataset_io.h>
#include <basalt/utils/filesystem.h>

#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace basalt {

// Dataset layout:
//   <path>/<clip>.MP4
//   <path>/<clip>.json
//
// The JSON is the format emitted by ORB-SLAM3's gopro_extract_imu.py.  Its
// accelerometer and gyroscope values have already been aligned to the camera
// calibration frame, so this reader deliberately does not apply another axis
// permutation.
class GoProVioDataset : public VioDataset {
 public:
  size_t get_num_cams() const override { return 1; }

  std::vector<int64_t>& get_image_timestamps() override {
    return image_timestamps;
  }

  const Eigen::aligned_vector<AccelData>& get_accel_data() const override {
    return accel_data;
  }

  const Eigen::aligned_vector<GyroData>& get_gyro_data() const override {
    return gyro_data;
  }

  const std::vector<int64_t>& get_gt_timestamps() const override {
    return gt_timestamps;
  }

  const Eigen::aligned_vector<Sophus::SE3d>& get_gt_pose_data()
      const override {
    return gt_pose_data;
  }

  int64_t get_mocap_to_imu_offset_ns() const override { return 0; }

  std::vector<ImageData> get_image_data(int64_t t_ns) override {
    std::lock_guard<std::mutex> lock(video_mutex);

    const auto it = image_index.find(t_ns);
    if (it == image_index.end()) {
      throw std::out_of_range("Unknown GoPro image timestamp");
    }

    const size_t frame_index = it->second;
    if (frame_index != next_frame_index) {
      if (!video.set(cv::CAP_PROP_POS_FRAMES, static_cast<double>(frame_index))) {
        throw std::runtime_error("Could not seek GoPro video frame");
      }
    }

    cv::Mat frame;
    if (!video.read(frame) || frame.empty()) {
      throw std::runtime_error("Could not decode GoPro video frame " +
                               std::to_string(frame_index));
    }
    next_frame_index = frame_index + 1;

    cv::Mat gray;
    if (frame.channels() == 1) {
      gray = frame;
    } else if (frame.channels() == 3) {
      cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else if (frame.channels() == 4) {
      cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
    } else {
      throw std::runtime_error("Unsupported GoPro video pixel format");
    }

    ImageData image_data;
    image_data.img.reset(new ManagedImage<uint16_t>(gray.cols, gray.rows));

    if (gray.type() == CV_8UC1) {
      for (int y = 0; y < gray.rows; ++y) {
        const uint8_t* src = gray.ptr<uint8_t>(y);
        uint16_t* dst = image_data.img->ptr + y * gray.cols;
        for (int x = 0; x < gray.cols; ++x) {
          dst[x] = static_cast<uint16_t>(src[x]) << 8;
        }
      }
    } else if (gray.type() == CV_16UC1) {
      for (int y = 0; y < gray.rows; ++y) {
        const uint16_t* src = gray.ptr<uint16_t>(y);
        uint16_t* dst = image_data.img->ptr + y * gray.cols;
        std::copy(src, src + gray.cols, dst);
      }
    } else {
      throw std::runtime_error("Unsupported GoPro grayscale pixel format");
    }

    return {std::move(image_data)};
  }

 private:
  friend class GoProIO;

  std::vector<int64_t> image_timestamps;
  std::unordered_map<int64_t, size_t> image_index;
  Eigen::aligned_vector<AccelData> accel_data;
  Eigen::aligned_vector<GyroData> gyro_data;
  std::vector<int64_t> gt_timestamps;
  Eigen::aligned_vector<Sophus::SE3d> gt_pose_data;
  cv::VideoCapture video;
  size_t next_frame_index = 0;
  std::mutex video_mutex;
};

class GoProIO : public DatasetIoInterface {
 public:
  void read(const std::string& path) override {
    if (!fs::is_directory(path)) {
      throw std::runtime_error("GoPro dataset directory does not exist: " + path);
    }

    const fs::path video_path = find_video(path);
    const fs::path telemetry_path = find_telemetry(video_path);

    std::ifstream telemetry_file(telemetry_path.string());
    if (!telemetry_file.is_open()) {
      throw std::runtime_error("Could not open GoPro telemetry: " +
                               telemetry_path.string());
    }

    nlohmann::json telemetry;
    telemetry_file >> telemetry;
    load_telemetry(telemetry);

    // OpenCV's FFmpeg backend is not safe to use concurrently with its frame
    // threading enabled in this GUI application.  GoPro H.265 clips decode
    // reliably with a single decoder thread; access is also serialized in
    // get_image_data above.
    data->video.open(video_path.string(), cv::CAP_FFMPEG,
                     {cv::CAP_PROP_N_THREADS, 1});
    if (!data->video.isOpened()) {
      data->video.open(video_path.string());
    }
    if (!data->video.isOpened()) {
      throw std::runtime_error("Could not open GoPro video: " +
                               video_path.string());
    }

    const size_t video_frames = static_cast<size_t>(std::llround(
        data->video.get(cv::CAP_PROP_FRAME_COUNT)));
    if (video_frames > 0 && video_frames < data->image_timestamps.size()) {
      throw std::runtime_error(
          "GoPro telemetry contains more image timestamps than video frames");
    }

    std::cout << "Loaded GoPro dataset with " << data->image_timestamps.size()
              << " frames and " << data->gyro_data.size() << " IMU samples"
              << std::endl;
  }

  void reset() override { data.reset(); }

  VioDatasetPtr get_data() override { return data; }

 private:
  static std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value;
  }

  static fs::path find_video(const std::string& path) {
    std::vector<fs::path> videos;
    for (const auto& entry : fs::directory_iterator(path)) {
      if (entry.is_regular_file() &&
          lowercase(entry.path().extension().string()) == ".mp4") {
        videos.emplace_back(entry.path());
      }
    }
    std::sort(videos.begin(), videos.end());
    if (videos.size() != 1) {
      throw std::runtime_error(
          "GoPro dataset directory must contain exactly one MP4 video");
    }
    return videos.front();
  }

  static fs::path find_telemetry(const fs::path& video_path) {
    const fs::path telemetry_path = video_path.parent_path() /
                                    (video_path.stem().string() + ".json");
    if (!fs::exists(telemetry_path)) {
      throw std::runtime_error("Missing telemetry JSON next to GoPro video: " +
                               telemetry_path.string());
    }
    return telemetry_path;
  }

  void load_telemetry(const nlohmann::json& telemetry) {
    constexpr const char* kAccel = "accelerometer";
    constexpr const char* kGyro = "gyroscope";
    constexpr const char* kImuTimestamps = "timestamps_ns";
    constexpr const char* kImageTimestamps = "img_timestamps_ns";

    if (!telemetry.contains(kAccel) || !telemetry.contains(kGyro) ||
        !telemetry.contains(kImuTimestamps) ||
        !telemetry.contains(kImageTimestamps)) {
      throw std::runtime_error(
          "Unsupported GoPro telemetry JSON; expected accelerometer, "
          "gyroscope, timestamps_ns and img_timestamps_ns");
    }

    const auto& accel = telemetry.at(kAccel);
    const auto& gyro = telemetry.at(kGyro);
    const auto& imu_timestamps = telemetry.at(kImuTimestamps);
    const auto& image_timestamps = telemetry.at(kImageTimestamps);
    if (accel.empty() || image_timestamps.empty() || accel.size() != gyro.size() ||
        accel.size() != imu_timestamps.size()) {
      throw std::runtime_error("GoPro telemetry arrays have incompatible sizes");
    }

    data.reset(new GoProVioDataset);
    data->accel_data.reserve(accel.size());
    data->gyro_data.reserve(gyro.size());
    for (size_t i = 0; i < accel.size(); ++i) {
      if (!accel[i].is_array() || !gyro[i].is_array() || accel[i].size() != 3 ||
          gyro[i].size() != 3) {
        throw std::runtime_error("Invalid GoPro IMU sample at index " +
                                 std::to_string(i));
      }

      const int64_t t_ns =
          static_cast<int64_t>(std::llround(imu_timestamps[i].get<double>()));
      if (i > 0 && t_ns <= data->gyro_data.back().timestamp_ns) {
        throw std::runtime_error("GoPro IMU timestamps are not strictly increasing");
      }

      AccelData accel_data;
      accel_data.timestamp_ns = t_ns;
      accel_data.data = Eigen::Vector3d(accel[i][0].get<double>(),
                                        accel[i][1].get<double>(),
                                        accel[i][2].get<double>());
      data->accel_data.emplace_back(accel_data);

      GyroData gyro_data;
      gyro_data.timestamp_ns = t_ns;
      gyro_data.data = Eigen::Vector3d(gyro[i][0].get<double>(),
                                       gyro[i][1].get<double>(),
                                       gyro[i][2].get<double>());
      data->gyro_data.emplace_back(gyro_data);
    }

    data->image_timestamps.reserve(image_timestamps.size());
    for (size_t i = 0; i < image_timestamps.size(); ++i) {
      const int64_t t_ns = static_cast<int64_t>(
          std::llround(image_timestamps[i].get<double>()));
      if (!data->image_timestamps.empty() &&
          t_ns <= data->image_timestamps.back()) {
        throw std::runtime_error(
            "GoPro image timestamps are not strictly increasing");
      }
      data->image_index.emplace(t_ns, i);
      data->image_timestamps.emplace_back(t_ns);
    }
  }

  std::shared_ptr<GoProVioDataset> data;
};

}  // namespace basalt
