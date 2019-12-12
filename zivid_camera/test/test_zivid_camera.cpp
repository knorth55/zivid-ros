#ifdef __clang__
#pragma clang diagnostic push
// Errors to ignore for this entire file
#pragma clang diagnostic ignored "-Wglobal-constructors"  // error triggered by gtest fixtures
#endif

#include <zivid_camera/CameraInfoSerialNumber.h>
#include <zivid_camera/CameraInfoModelName.h>
#include <zivid_camera/Capture.h>
#include <zivid_camera/Capture2D.h>
#include <zivid_camera/CaptureAssistantSuggestSettings.h>
#include <zivid_camera/CaptureFrameConfig.h>
#include <zivid_camera/Capture2DFrameConfig.h>
#include <zivid_camera/CaptureGeneralConfig.h>
#include <zivid_camera/IsConnected.h>

#include <Zivid/Application.h>
#include <Zivid/CaptureAssistant.h>
#include <Zivid/Frame.h>
#include <Zivid/Camera.h>
#include <Zivid/Version.h>

#include <dynamic_reconfigure/client.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/Image.h>

#include "gtest_include_wrapper.h"

#include <ros/ros.h>

using SecondsD = std::chrono::duration<double>;

namespace
{
template <class Rep, class Period>
ros::Duration toRosDuration(const std::chrono::duration<Rep, Period>& d)
{
  return ros::Duration{ std::chrono::duration_cast<SecondsD>(d).count() };
}
}  // namespace

class ZividNodeTest : public testing::Test
{
protected:
  ros::NodeHandle nh_;

  const ros::Duration node_ready_wait_duration{ 15 };
  const ros::Duration short_wait_duration{ 0.25 };
  const ros::Duration dr_get_max_wait_duration{ 1 };
  static constexpr auto capture_service_name = "/zivid_camera/capture";
  static constexpr auto capture_2d_service_name = "/zivid_camera/capture_2d";
  static constexpr auto capture_assistant_suggest_settings_service_name = "/zivid_camera/capture_assistant/"
                                                                          "suggest_settings";
  static constexpr auto color_camera_info_topic_name = "/zivid_camera/color/camera_info";
  static constexpr auto color_image_color_topic_name = "/zivid_camera/color/image_color";
  static constexpr auto depth_camera_info_topic_name = "/zivid_camera/depth/camera_info";
  static constexpr auto depth_image_raw_topic_name = "/zivid_camera/depth/image_raw";
  static constexpr auto points_topic_name = "/zivid_camera/points";
  static constexpr size_t num_dr_capture_servers = 10;

  class SubscriptionWrapper
  {
  public:
    template <class Type, class Fn>
    static SubscriptionWrapper make(ros::NodeHandle& nh, const std::string& name, Fn&& fn)
    {
      SubscriptionWrapper w;
      boost::function<void(const boost::shared_ptr<const Type>&)> cb = [ptr = w.num_messages_.get(),
                                                                        fn = std::move(fn)](const auto& v) mutable {
        (*ptr)++;
        fn(v);
      };
      w.subscriber_ = nh.subscribe<Type>(name, 1, cb);
      return w;
    }

    std::size_t numMessages() const
    {
      return *num_messages_;
    }

  private:
    SubscriptionWrapper() : num_messages_(std::make_unique<std::size_t>(0))
    {
    }
    ros::Subscriber subscriber_;
    std::unique_ptr<std::size_t> num_messages_;
  };

  void spinOnce()
  {
    ros::spinOnce();
  }

  void sleepAndSpin(ros::Duration duration)
  {
    duration.sleep();
    spinOnce();
  }

  void waitForReady()
  {
    ASSERT_TRUE(ros::service::waitForService(capture_service_name, node_ready_wait_duration));
  }

  void enableFirst3DFrame()
  {
    dynamic_reconfigure::Client<zivid_camera::CaptureFrameConfig> frame_0_client("/zivid_camera/capture/"
                                                                                 "frame_0/");
    sleepAndSpin(dr_get_max_wait_duration);
    zivid_camera::CaptureFrameConfig frame_0_cfg;
    ASSERT_TRUE(frame_0_client.getDefaultConfiguration(frame_0_cfg, dr_get_max_wait_duration));
    frame_0_cfg.enabled = true;
    ASSERT_TRUE(frame_0_client.setConfiguration(frame_0_cfg));
  }

  void enableFirst2DFrame()
  {
    dynamic_reconfigure::Client<zivid_camera::Capture2DFrameConfig> frame_0_client("/zivid_camera/capture_2d/"
                                                                                   "frame_0/");
    sleepAndSpin(dr_get_max_wait_duration);
    zivid_camera::Capture2DFrameConfig cfg;
    ASSERT_TRUE(frame_0_client.getDefaultConfiguration(cfg, dr_get_max_wait_duration));
    cfg.enabled = true;
    ASSERT_TRUE(frame_0_client.setConfiguration(cfg));
  }

  template <class Type, class Fn>
  SubscriptionWrapper subscribe(const std::string& name, Fn&& callback)
  {
    return SubscriptionWrapper::make<Type>(nh_, name, callback);
  }

  template <class Type>
  SubscriptionWrapper subscribe(const std::string& name)
  {
    return subscribe<Type>(name, [](const auto&) {});
  }

  template <class A, class B>
  void assertArrayFloatEq(const A& actual, const B& expected)
  {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < actual.size(); i++)
    {
      ASSERT_FLOAT_EQ(actual[i], expected[i]);
    }
  }

  void assertCameraInfoForFileCamera(const sensor_msgs::CameraInfo& ci)
  {
    ASSERT_EQ(ci.width, 1920U);
    ASSERT_EQ(ci.height, 1200U);
    ASSERT_EQ(ci.distortion_model, "plumb_bob");

    //     [fx  0 cx]
    // K = [ 0 fy cy]
    //     [ 0  0  1]
    assertArrayFloatEq(
        ci.K, std::array<double, 9>{ 2759.12329102, 0, 958.78460693, 0, 2758.73681641, 634.94018555, 0, 0, 1 });

    // R = I
    assertArrayFloatEq(ci.R, std::array<double, 9>{ 1, 0, 0, 0, 1, 0, 0, 0, 1 });

    //     [fx'  0  cx' Tx]
    // P = [ 0  fy' cy' Ty]
    //     [ 0   0   1   0]
    assertArrayFloatEq(ci.P, std::array<double, 12>{ 2759.12329102, 0, 958.78460693, 0, 0, 2758.73681641, 634.94018555,
                                                     0, 0, 0, 1, 0 });
  }

  struct FileCameraExpectedRGB
  {
    std::size_t row, col;
    unsigned char r, g, b;
  };
  // The expected RGB's are found by checking the color image of a frame produced by the file
  // camera (MiscObjects.zdf)
  std::array<FileCameraExpectedRGB, 4> miscObjectsExpectedRGBs = { FileCameraExpectedRGB{ 0, 0, 4, 4, 2 },
                                                                   FileCameraExpectedRGB{ 1199, 1919, 10, 8, 7 },
                                                                   FileCameraExpectedRGB{ 280, 1500, 255, 183, 42 },
                                                                   FileCameraExpectedRGB{ 700, 800, 120, 105, 82 } };
};

TEST_F(ZividNodeTest, testServiceCameraInfoModelName)
{
  waitForReady();
  zivid_camera::CameraInfoModelName model_name;
  ASSERT_TRUE(ros::service::call("/zivid_camera/camera_info/model_name", model_name));
  ASSERT_EQ(model_name.response.model_name, std::string("FileCamera-") + ZIVID_VERSION);
}

TEST_F(ZividNodeTest, testServiceCameraInfoSerialNumber)
{
  waitForReady();
  zivid_camera::CameraInfoSerialNumber serial_number;
  ASSERT_TRUE(ros::service::call("/zivid_camera/camera_info/serial_number", serial_number));
  ASSERT_EQ(serial_number.response.serial_number, "F1");
}

TEST_F(ZividNodeTest, testServiceIsConnected)
{
  waitForReady();
  zivid_camera::IsConnected is_connected;
  ASSERT_TRUE(ros::service::call("/zivid_camera/is_connected", is_connected));
  ASSERT_EQ(is_connected.response.is_connected, true);
}

TEST_F(ZividNodeTest, testCapturePublishesTopics)
{
  waitForReady();

  auto color_camera_info_sub = subscribe<sensor_msgs::CameraInfo>(color_camera_info_topic_name);
  auto color_image_color_sub = subscribe<sensor_msgs::Image>(color_image_color_topic_name);
  auto depth_camera_info_sub = subscribe<sensor_msgs::CameraInfo>(depth_camera_info_topic_name);
  auto depth_image_raw_sub = subscribe<sensor_msgs::Image>(depth_image_raw_topic_name);
  auto points_sub = subscribe<sensor_msgs::PointCloud2>(points_topic_name);

  auto assert_num_topics_received = [&](std::size_t numTopics) {
    ASSERT_EQ(color_camera_info_sub.numMessages(), numTopics);
    ASSERT_EQ(color_image_color_sub.numMessages(), numTopics);
    ASSERT_EQ(depth_camera_info_sub.numMessages(), numTopics);
    ASSERT_EQ(depth_image_raw_sub.numMessages(), numTopics);
    ASSERT_EQ(points_sub.numMessages(), numTopics);
  };

  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(0);

  zivid_camera::Capture capture;
  // Capture fails when no frames are enabled
  ASSERT_FALSE(ros::service::call(capture_service_name, capture));
  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(0);

  enableFirst3DFrame();

  ASSERT_TRUE(ros::service::call(capture_service_name, capture));
  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(1);

  ASSERT_TRUE(ros::service::call(capture_service_name, capture));
  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(2);

  ASSERT_TRUE(ros::service::call(capture_service_name, capture));
  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(3);

  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(3);
}

TEST_F(ZividNodeTest, testCapturePoints)
{
  waitForReady();

  std::optional<sensor_msgs::PointCloud2> last_pc2;
  auto points_sub = subscribe<sensor_msgs::PointCloud2>(points_topic_name, [&](const auto& p) { last_pc2 = *p; });
  enableFirst3DFrame();
  zivid_camera::Capture capture;
  ASSERT_TRUE(ros::service::call(capture_service_name, capture));
  spinOnce();

  ASSERT_TRUE(last_pc2.has_value());
  ASSERT_EQ(last_pc2->width, 1920U);
  ASSERT_EQ(last_pc2->height, 1200U);
  ASSERT_EQ(last_pc2->point_step, 20U);
  ASSERT_EQ(last_pc2->row_step, 1920U * 20U);
  ASSERT_EQ(last_pc2->is_dense, false);
  ASSERT_EQ(last_pc2->data.size(), 1920U * 1200U * 20U);

  const std::size_t test_point_x = 1500;
  const std::size_t test_point_y = 320;

  Zivid::Application zivid;
  auto camera = zivid.createFileCamera("/usr/share/Zivid/data/MiscObjects.zdf");
  const auto point = camera.capture().getPointCloud()(test_point_y, test_point_x);

  const std::size_t idx = test_point_y * last_pc2->row_step + test_point_x * last_pc2->point_step;

  uint8_t* point_ptr = &(last_pc2->data[idx]);
  const float x = *reinterpret_cast<float*>(&(point_ptr[0]));
  const float y = *reinterpret_cast<float*>(&(point_ptr[4]));
  const float z = *reinterpret_cast<float*>(&(point_ptr[8]));
  const float contrast = *reinterpret_cast<float*>(&(point_ptr[12]));
  const uint32_t rgba = *reinterpret_cast<uint32_t*>(&(point_ptr[16]));

  const float delta = 0.00001f;
  ASSERT_NEAR(x, point.x / 1000, delta);
  ASSERT_NEAR(y, point.y / 1000, delta);
  ASSERT_NEAR(z, point.z / 1000, delta);
  ASSERT_NEAR(contrast, point.contrast, delta);
  ASSERT_EQ(rgba, point.rgba);
}

TEST_F(ZividNodeTest, testCaptureImage)
{
  waitForReady();

  std::optional<sensor_msgs::Image> image;
  auto color_image_sub =
      subscribe<sensor_msgs::Image>(color_image_color_topic_name, [&](const auto& i) { image = *i; });
  enableFirst3DFrame();
  zivid_camera::Capture capture;
  ASSERT_TRUE(ros::service::call(capture_service_name, capture));
  sleepAndSpin(short_wait_duration);
  ASSERT_TRUE(image.has_value());
  ASSERT_EQ(image->width, 1920U);
  ASSERT_EQ(image->height, 1200U);
  constexpr uint32_t bytes_per_pixel = 3U;
  ASSERT_EQ(image->step, bytes_per_pixel * 1920U);
  ASSERT_EQ(image->data.size(), image->step * image->height);
  ASSERT_EQ(image->encoding, "rgb8");
  ASSERT_EQ(image->is_bigendian, false);

  auto verifyPixelColor = [&](const FileCameraExpectedRGB& expectedRGB) {
    const auto index = expectedRGB.row * image->step + 3 * expectedRGB.col;
    ASSERT_EQ(image->data[index], expectedRGB.r);
    ASSERT_EQ(image->data[index + 1], expectedRGB.g);
    ASSERT_EQ(image->data[index + 2], expectedRGB.b);
  };

  for (const auto& expectedRGB : miscObjectsExpectedRGBs)
  {
    verifyPixelColor(expectedRGB);
  }
}

TEST_F(ZividNodeTest, testCaptureCameraInfo)
{
  waitForReady();

  std::optional<sensor_msgs::CameraInfo> color_camera_info;
  auto color_camera_info_sub =
      subscribe<sensor_msgs::CameraInfo>(color_camera_info_topic_name, [&](const auto& r) { color_camera_info = *r; });

  std::optional<sensor_msgs::CameraInfo> depth_camera_info;
  auto depth_camera_info_sub =
      subscribe<sensor_msgs::CameraInfo>(depth_camera_info_topic_name, [&](const auto& r) { depth_camera_info = *r; });

  enableFirst3DFrame();
  zivid_camera::Capture capture;
  ASSERT_TRUE(ros::service::call(capture_service_name, capture));
  sleepAndSpin(short_wait_duration);

  ASSERT_EQ(color_camera_info_sub.numMessages(), 1U);
  ASSERT_EQ(depth_camera_info_sub.numMessages(), 1U);

  ASSERT_TRUE(color_camera_info.has_value());
  assertCameraInfoForFileCamera(*color_camera_info);
  ASSERT_TRUE(depth_camera_info.has_value());
  assertCameraInfoForFileCamera(*depth_camera_info);
}

TEST_F(ZividNodeTest, test3DSettingsDynamicReconfigureNodesAreAvailable)
{
  waitForReady();

  const std::string prefix = "/zivid_camera/capture/";

  ASSERT_TRUE(ros::service::waitForService(prefix + "general/set_parameters", short_wait_duration));
  for (std::size_t i = 0; i < 10U; i++)
  {
    ASSERT_TRUE(
        ros::service::waitForService(prefix + "frame_" + std::to_string(i) + "/set_parameters", short_wait_duration));
  }
  ASSERT_FALSE(ros::service::waitForService(prefix + "frame_11/set_parameters", short_wait_duration));
}

TEST_F(ZividNodeTest, testCapture2D)
{
  waitForReady();

  std::optional<sensor_msgs::CameraInfo> color_camera_info;
  std::optional<sensor_msgs::Image> image;
  auto color_camera_info_sub =
      subscribe<sensor_msgs::CameraInfo>(color_camera_info_topic_name, [&](const auto& r) { color_camera_info = *r; });
  auto color_image_color_sub =
      subscribe<sensor_msgs::Image>(color_image_color_topic_name, [&](const auto& i) { image = *i; });

  auto assert_num_topics_received = [&](std::size_t numTopics) {
    ASSERT_EQ(color_camera_info_sub.numMessages(), numTopics);
    ASSERT_EQ(color_image_color_sub.numMessages(), numTopics);
  };

  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(0);

  // Capture fails when no frames are enabled
  zivid_camera::Capture2D capture;
  ASSERT_FALSE(ros::service::call(capture_2d_service_name, capture));
  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(0);

  enableFirst2DFrame();
  ASSERT_TRUE(ros::service::call(capture_2d_service_name, capture));
  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(1);

  auto verifyImageAndCameraInfo = [this](const auto& img, const auto& info) {
    assertCameraInfoForFileCamera(info);

    ASSERT_EQ(img.width, 1920U);
    ASSERT_EQ(img.height, 1200U);
    constexpr uint32_t bytes_per_pixel = 4U;
    ASSERT_EQ(img.step, bytes_per_pixel * 1920U);
    ASSERT_EQ(img.encoding, "rgba8");
    ASSERT_EQ(img.is_bigendian, false);
    ASSERT_EQ(img.data.size(), img.step * img.height);

    auto verifyPixelColor = [&](const FileCameraExpectedRGB& expectedRGB) {
      constexpr unsigned char expectedA = 255;
      const auto index = expectedRGB.row * img.step + bytes_per_pixel * expectedRGB.col;
      ASSERT_EQ(img.data[index], expectedRGB.r);
      ASSERT_EQ(img.data[index + 1], expectedRGB.g);
      ASSERT_EQ(img.data[index + 2], expectedRGB.b);
      ASSERT_EQ(img.data[index + 3], expectedA);
    };

    for (const auto& expectedRGB : miscObjectsExpectedRGBs)
    {
      verifyPixelColor(expectedRGB);
    }
  };

  ASSERT_TRUE(image.has_value());
  ASSERT_TRUE(color_camera_info.has_value());
  verifyImageAndCameraInfo(*image, *color_camera_info);

  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(1);

  ASSERT_TRUE(ros::service::call(capture_2d_service_name, capture));
  sleepAndSpin(short_wait_duration);
  assert_num_topics_received(2);
  verifyImageAndCameraInfo(*image, *color_camera_info);
}

TEST_F(ZividNodeTest, test2DSettingsDynamicReconfigureNodesAreAvailable)
{
  waitForReady();

  const std::string prefix = "/zivid_camera/capture_2d/";
  ASSERT_TRUE(ros::service::waitForService(prefix + "frame_0/set_parameters", short_wait_duration));
  ASSERT_FALSE(ros::service::waitForService(prefix + "frame_1/set_parameters", short_wait_duration));
}

class ZividCATest : public ZividNodeTest
{
protected:
  ZividCATest()
    : m_camera(m_zivid.createFileCamera("/usr/share/Zivid/data/MiscObjects.zdf"))
    , m_captureGeneralClient("/zivid_camera/capture/general")
  {
    waitForReady();
    m_captureFrameClients.reserve(num_dr_capture_servers);
    for (std::size_t i = 0; i < num_dr_capture_servers; i++)
    {
      using Client = dynamic_reconfigure::Client<zivid_camera::CaptureFrameConfig>;
      m_captureFrameClients.emplace_back(std::make_unique<Client>("/zivid_camera/capture/frame_" + std::to_string(i)));
    }
  }

  zivid_camera::CaptureGeneralConfig captureGeneralConfig()
  {
    zivid_camera::CaptureGeneralConfig cfg;
    m_captureGeneralClient.getCurrentConfiguration(cfg, dr_get_max_wait_duration);
    return cfg;
  }

  zivid_camera::CaptureFrameConfig captureFrameConfig(std::size_t i) const
  {
    zivid_camera::CaptureFrameConfig cfg;
    m_captureFrameClients[i]->getCurrentConfiguration(cfg, dr_get_max_wait_duration);
    return cfg;
  }

  std::size_t numEnabled3DFrames() const
  {
    std::size_t num_enabled_3d_frames = 0;
    for (std::size_t i = 0; i < num_dr_capture_servers; i++)
    {
      if (captureFrameConfig(i).enabled)
      {
        num_enabled_3d_frames++;
      }
    }
    return num_enabled_3d_frames;
  }

  void compareCaptureFrameConfigWithSettings(const Zivid::Settings& s,
                                             const zivid_camera::CaptureFrameConfig& cfg) const
  {
    ASSERT_EQ(true, cfg.enabled);
    ASSERT_EQ(s.bidirectional().value(), cfg.bidirectional);
    ASSERT_EQ(s.brightness().value(), cfg.brightness);
    ASSERT_EQ(s.exposureTime().value().count(), cfg.exposure_time);
    ASSERT_EQ(s.gain().value(), cfg.gain);
    ASSERT_EQ(s.iris().value(), static_cast<std::size_t>(cfg.iris));
  }

  void compareCaptureGeneralConfigWithSettings(const Zivid::Settings& s,
                                               const zivid_camera::CaptureGeneralConfig& cfg) const
  {
    ASSERT_EQ(s.blueBalance().value(), cfg.blue_balance);
    ASSERT_EQ(s.filters().contrast().isEnabled().value(), cfg.filters_contrast_enabled);
    ASSERT_EQ(s.filters().contrast().threshold().value(), cfg.filters_contrast_threshold);
    ASSERT_EQ(s.filters().gaussian().isEnabled().value(), cfg.filters_gaussian_enabled);
    ASSERT_EQ(s.filters().gaussian().sigma().value(), cfg.filters_gaussian_sigma);
    ASSERT_EQ(s.filters().outlier().isEnabled().value(), cfg.filters_outlier_enabled);
    ASSERT_EQ(s.filters().outlier().threshold().value(), cfg.filters_outlier_threshold);
    ASSERT_EQ(s.filters().reflection().isEnabled().value(), cfg.filters_reflection_enabled);
    ASSERT_EQ(s.filters().saturated().isEnabled().value(), cfg.filters_saturated_enabled);
    ASSERT_EQ(s.redBalance().value(), cfg.red_balance);
  }

  Zivid::CaptureAssistant::AmbientLightFrequency toAPIAmbientLightFrequency(
      zivid_camera::CaptureAssistantSuggestSettings::Request::_ambient_light_frequency_type ambient_light_frequency)
  {
    using Request = zivid_camera::CaptureAssistantSuggestSettings::Request;
    switch (ambient_light_frequency)
    {
      case Request::AMBIENT_LIGHT_FREQUENCY_NONE:
        return Zivid::CaptureAssistant::AmbientLightFrequency::none;
      case Request::AMBIENT_LIGHT_FREQUENCY_50HZ:
        return Zivid::CaptureAssistant::AmbientLightFrequency::hz50;
      case Request::AMBIENT_LIGHT_FREQUENCY_60HZ:
        return Zivid::CaptureAssistant::AmbientLightFrequency::hz60;
    }
    throw std::runtime_error("Could not convert value " + std::to_string(ambient_light_frequency) + " to API enum.");
  }

  void performSuggestSettingsAndCompareWithCppAPI(
      ros::Duration max_capture_time,
      zivid_camera::CaptureAssistantSuggestSettings::Request::_ambient_light_frequency_type ambient_light_frequency)
  {
    zivid_camera::CaptureAssistantSuggestSettings srv;
    srv.request.max_capture_time = max_capture_time;
    srv.request.ambient_light_frequency = ambient_light_frequency;
    ASSERT_TRUE(ros::service::call(capture_assistant_suggest_settings_service_name, srv));
    sleepAndSpin(short_wait_duration);

    Zivid::CaptureAssistant::SuggestSettingsParameters suggest_settings_parameters(
        std::chrono::round<std::chrono::milliseconds>(SecondsD{ max_capture_time.toSec() }),
        toAPIAmbientLightFrequency(ambient_light_frequency));
    const auto api_settings{ Zivid::CaptureAssistant::suggestSettings(m_camera, suggest_settings_parameters) };

    ASSERT_EQ(api_settings.size(), numEnabled3DFrames());
    compareCaptureGeneralConfigWithSettings(api_settings[0], captureGeneralConfig());
    for (std::size_t i = 0; i < api_settings.size(); i++)
    {
      compareCaptureFrameConfigWithSettings(api_settings[i], captureFrameConfig(i));
    }
    for (std::size_t i = api_settings.size(); i < num_dr_capture_servers; i++)
    {
      ASSERT_EQ(false, captureFrameConfig(i).enabled);
    }
  }

private:
  Zivid::Application m_zivid;
  Zivid::Camera m_camera;
  dynamic_reconfigure::Client<zivid_camera::CaptureGeneralConfig> m_captureGeneralClient;
  std::vector<std::unique_ptr<dynamic_reconfigure::Client<zivid_camera::CaptureFrameConfig>>> m_captureFrameClients;
};

TEST_F(ZividCATest, test_ca_service_is_available)
{
  ASSERT_TRUE(ros::service::waitForService(capture_assistant_suggest_settings_service_name, short_wait_duration));
}

// TODO(10): Enable this test when minimum Zivid SDK is bumped to 1.8
TEST_F(ZividCATest, DISABLED_test_different_max_capture_time_and_ambient_light_frequency)
{
  using Request = zivid_camera::CaptureAssistantSuggestSettings::Request;
  for (double max_capture_time : { 0.2, 1.2, 10.0 })
  {
    for (auto ambient_light_frequency : { Request::AMBIENT_LIGHT_FREQUENCY_NONE, Request::AMBIENT_LIGHT_FREQUENCY_50HZ,
                                          Request::AMBIENT_LIGHT_FREQUENCY_60HZ })
    {
      performSuggestSettingsAndCompareWithCppAPI(ros::Duration{ max_capture_time }, ambient_light_frequency);
    }
  }
}

// TODO(10): Enable this test when minimum Zivid SDK is bumped to 1.8
TEST_F(ZividCATest, DISABLED_test_going_from_several_frames_to_1_frame)
{
  using Request = zivid_camera::CaptureAssistantSuggestSettings::Request;
  performSuggestSettingsAndCompareWithCppAPI(ros::Duration{ 10.0 }, Request::AMBIENT_LIGHT_FREQUENCY_NONE);
  ASSERT_GT(numEnabled3DFrames(), 1U);

  performSuggestSettingsAndCompareWithCppAPI(ros::Duration{ 0.2 }, Request::AMBIENT_LIGHT_FREQUENCY_NONE);
  ASSERT_EQ(numEnabled3DFrames(), 1U);
}

TEST_F(ZividCATest, test_capture_assistant_with_invalid_max_capture_time_fails)
{
  zivid_camera::CaptureAssistantSuggestSettings srv;
  srv.request.max_capture_time = ros::Duration{ 0.0 };
  ASSERT_FALSE(ros::service::call(capture_assistant_suggest_settings_service_name, srv));

  const auto& validRange = Zivid::CaptureAssistant::maxCaptureTimeRange;
  const auto smallDelta = std::chrono::milliseconds{ 1 };
  srv.request.max_capture_time = toRosDuration(validRange.min() - smallDelta);
  ASSERT_FALSE(ros::service::call(capture_assistant_suggest_settings_service_name, srv));
  srv.request.max_capture_time = toRosDuration(validRange.max() + smallDelta);
  ASSERT_FALSE(ros::service::call(capture_assistant_suggest_settings_service_name, srv));

  // TODO(10): Enable remainder of test when minimum Zivid SDK is bumped to 1.8
  // srv.request.max_capture_time = ros::Duration{ 10.0 };
  // ASSERT_TRUE(ros::service::call(capture_assistant_suggest_settings_service_name, srv));
}

// TODO(10): Enable this test when minimum Zivid SDK is bumped to 1.8
TEST_F(ZividCATest, DISABLED_test_capture_assistant_default_ambient_light_frequency_works)
{
  zivid_camera::CaptureAssistantSuggestSettings srv;
  srv.request.max_capture_time = ros::Duration{ 1.0 };
  ASSERT_TRUE(ros::service::call(capture_assistant_suggest_settings_service_name, srv));
}

TEST_F(ZividCATest, test_capture_assistant_invalid_ambient_light_frequency_fails)
{
  zivid_camera::CaptureAssistantSuggestSettings srv;
  srv.request.max_capture_time = ros::Duration{ 1.0 };
  srv.request.ambient_light_frequency = 255;
  ASSERT_FALSE(ros::service::call(capture_assistant_suggest_settings_service_name, srv));

  // TODO(10): Enable remainder of test when minimum Zivid SDK is bumped to 1.8
  // srv.request.ambient_light_frequency =
  //   zivid_camera::CaptureAssistantSuggestSettings::Request::AMBIENT_LIGHT_FREQUENCY_NONE;
  // ASSERT_TRUE(ros::service::call(capture_assistant_suggest_settings_service_name, srv));
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  ros::init(argc, argv, "test_zivid_camera");
  ros::NodeHandle nh;
  return RUN_ALL_TESTS();
}
