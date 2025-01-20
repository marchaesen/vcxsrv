/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <xtensor/xrandom.hpp>

#include <iostream>
#include "tensorflow/lite/c/c_api.h"
#include "test_executor.h"

#define TEST_CONV2D           1
#define TEST_DEPTHWISE        1
#define TEST_ADD              1
#define TEST_FULLY_CONNECTED  1
#define TEST_MOBILENETV1      1
#define TEST_MOBILEDET        1
#define TEST_YOLOX            1

#define TOLERANCE       2
#define MODEL_TOLERANCE 8
#define YOLOX_TOLERANCE 38
#define QUANT_TOLERANCE 2

std::vector<bool> is_signed{false}; /* TODO: Support INT8? */
std::vector<bool> padding_same{false, true};
std::vector<int> stride{1, 2};
std::vector<int> output_channels{1, 32, 120, 128, 160, 256};
std::vector<int> input_channels{1, 32, 120, 128, 256};
std::vector<int> dw_channels{1, 32, 120, 128, 256};
std::vector<int> dw_weight_size{3, 5};
std::vector<int> weight_size{1, 3, 5};
std::vector<int> input_size{3, 5, 8, 80, 112};
std::vector<int> fc_channels{23, 46, 128, 256, 512};
std::vector<int> fc_size{128, 1280, 25088, 62720};

static void
set_seed(unsigned seed)
{
   srand(seed);
   xt::random::seed(seed);
}

static void
test_model(void *buf, size_t buf_size, std::string cache_dir, unsigned tolerance)
{
   void **input = NULL;
   size_t num_inputs;
   void **cpu_output;
   size_t *output_sizes;
   TfLiteType *output_types;
   size_t num_outputs;
   void **npu_output;

   TfLiteModel *model = TfLiteModelCreate(buf, buf_size);
   assert(model);

   run_model(model, EXECUTOR_CPU, &input, &num_inputs, &cpu_output, &output_sizes, &output_types, &num_outputs, cache_dir);
   run_model(model, EXECUTOR_NPU, &input, &num_inputs, &npu_output, &output_sizes, &output_types, &num_outputs, cache_dir);

   for (size_t i = 0; i < num_outputs; i++) {
      for (size_t j = 0; j < output_sizes[i]; j++) {
         switch (output_types[i]) {
            case kTfLiteFloat32: {
               float *cpu = ((float**)cpu_output)[i];
               float *npu = ((float**)npu_output)[i];
               if (abs(cpu[j] - npu[j]) > tolerance) {
                  std::cout << "CPU: ";
                  for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                     std::cout << std::setfill('0') << std::setw(6) << cpu[k] << " ";
                  std::cout << "\n";
                  std::cout << "NPU: ";
                  for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                     std::cout << std::setfill('0') << std::setw(6) << npu[k] << " ";
                  std::cout << "\n";

                  FAIL() << "Output at " << j << " from the NPU (" << std::setfill('0') << std::setw(2) << npu[j] << ") doesn't match that from the CPU (" << std::setfill('0') << std::setw(2) << cpu[j] << ").";
               }
               break;
            }
            default: {
               uint8_t *cpu = ((uint8_t**)cpu_output)[i];
               uint8_t *npu = ((uint8_t**)npu_output)[i];
               if (abs(cpu[j] - npu[j]) > tolerance) {
                  std::cout << "CPU: ";
                  for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                     std::cout << std::setfill('0') << std::setw(2) << std::hex << int(cpu[k]) << " ";
                  std::cout << "\n";
                  std::cout << "NPU: ";
                  for (int k = 0; k < std::min(int(output_sizes[i]), 24); k++)
                     std::cout << std::setfill('0') << std::setw(2) << std::hex << int(npu[k]) << " ";
                  std::cout << "\n";

                  FAIL() << "Output at " << j << " from the NPU (" << std::setfill('0') << std::setw(2) << std::hex << int(npu[j]) << ") doesn't match that from the CPU (" << std::setfill('0') << std::setw(2) << std::hex << int(cpu[j]) << ").";
               }
               break;
            }
         }
      }
   }

   for (size_t i = 0; i < num_inputs; i++)
      free(input[i]);
   free(input);

   for (size_t i = 0; i < num_outputs; i++)
      free(cpu_output[i]);
   free(cpu_output);

   for (size_t i = 0; i < num_outputs; i++)
      free(npu_output[i]);
   free(npu_output);

   free(output_sizes);
   free(output_types);

   TfLiteModelDelete(model);
}

static void
test_model_file(std::string file_name, unsigned tolerance, bool use_cache)
{
   std::ostringstream cache_dir;

   if (use_cache)
      cache_dir << "/var/cache/teflon_tests/" << std::filesystem::path(file_name).stem().c_str();

   set_seed(4);

   std::ifstream model_file(file_name, std::ios::binary);
   std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(model_file)),
                               std::istreambuf_iterator<char>());
   test_model(buffer.data(), buffer.size(), cache_dir.str(), tolerance);
}

void
test_conv(int input_size, int weight_size, int input_channels, int output_channels,
          int stride, bool padding_same, bool is_signed, bool depthwise, int seed)
{
   void *buf = NULL;
   size_t buf_size;
   std::ostringstream cache_dir, model_cache;
   cache_dir << "/var/cache/teflon_tests/" << input_size << "_" << weight_size << "_" << input_channels << "_" << output_channels << "_" << stride << "_" << padding_same << "_" << is_signed << "_" << depthwise << "_" << seed;
   model_cache << cache_dir.str() << "/"
               << "model.tflite";

   if (weight_size > input_size)
      GTEST_SKIP();

   set_seed(seed);

   if (cache_is_enabled()) {
      if (access(model_cache.str().c_str(), F_OK) == 0) {
         buf = read_buf(model_cache.str().c_str(), &buf_size);
      }
   }

   if (buf == 0) {
      buf = conv2d_generate_model(input_size, weight_size,
                                  input_channels, output_channels,
                                  stride, padding_same, is_signed,
                                  depthwise,
                                  &buf_size);

      if (cache_is_enabled()) {
         if (access(cache_dir.str().c_str(), F_OK) != 0) {
            ASSERT_TRUE(std::filesystem::create_directories(cache_dir.str().c_str()));
         }
         std::ofstream file(model_cache.str().c_str(), std::ios::out | std::ios::binary);
         file.write(reinterpret_cast<const char *>(buf), buf_size);
         file.close();
      }
   }

   test_model(buf, buf_size, cache_dir.str(), TOLERANCE);
   free(buf);
}

void
test_add(int input_size, int weight_size, int input_channels, int output_channels,
         int stride, bool padding_same, bool is_signed, bool depthwise, int seed,
         unsigned tolerance)
{
   void *buf = NULL;
   size_t buf_size;
   std::ostringstream cache_dir, model_cache;
   cache_dir << "/var/cache/teflon_tests/"
             << "add_" << input_size << "_" << weight_size << "_" << input_channels << "_" << output_channels << "_" << stride << "_" << padding_same << "_" << is_signed << "_" << depthwise << "_" << seed;
   model_cache << cache_dir.str() << "/"
               << "model.tflite";

   if (weight_size > input_size)
      GTEST_SKIP();

   set_seed(seed);

   if (cache_is_enabled()) {
      if (access(model_cache.str().c_str(), F_OK) == 0) {
         buf = read_buf(model_cache.str().c_str(), &buf_size);
      }
   }

   if (buf == 0) {
      buf = add_generate_model(input_size, weight_size,
                               input_channels, output_channels,
                               stride, padding_same, is_signed,
                               depthwise,
                               &buf_size);

      if (cache_is_enabled()) {
         if (access(cache_dir.str().c_str(), F_OK) != 0) {
            ASSERT_TRUE(std::filesystem::create_directories(cache_dir.str().c_str()));
         }
         std::ofstream file(model_cache.str().c_str(), std::ios::out | std::ios::binary);
         file.write(reinterpret_cast<const char *>(buf), buf_size);
         file.close();
      }
   }

   test_model(buf, buf_size, cache_dir.str(), tolerance);
   free(buf);
}

void
test_fully_connected(int input_size, int output_channels, bool is_signed, int seed)
{
   void *buf = NULL;
   size_t buf_size;
   std::ostringstream cache_dir, model_cache;
   cache_dir << "/var/cache/teflon_tests/fc_" << input_size << "_" << output_channels << "_" << is_signed << "_" << seed;
   model_cache << cache_dir.str() << "/"
               << "model.tflite";

   set_seed(seed);

   if (cache_is_enabled()) {
      if (access(model_cache.str().c_str(), F_OK) == 0) {
         buf = read_buf(model_cache.str().c_str(), &buf_size);
      }
   }

   if (buf == 0) {
      buf = fully_connected_generate_model(input_size, output_channels, is_signed, &buf_size);

      if (cache_is_enabled()) {
         if (access(cache_dir.str().c_str(), F_OK) != 0) {
            ASSERT_TRUE(std::filesystem::create_directories(cache_dir.str().c_str()));
         }
         std::ofstream file(model_cache.str().c_str(), std::ios::out | std::ios::binary);
         file.write(reinterpret_cast<const char *>(buf), buf_size);
         file.close();
      }
   }

   test_model(buf, buf_size, cache_dir.str(), TOLERANCE);
   free(buf);
}

#if TEST_CONV2D

class Conv2D : public testing::TestWithParam<std::tuple<bool, bool, int, int, int, int, int>> {};

TEST_P(Conv2D, Op)
{
   test_conv(std::get<6>(GetParam()),
             std::get<5>(GetParam()),
             std::get<4>(GetParam()),
             std::get<3>(GetParam()),
             std::get<2>(GetParam()),
             std::get<1>(GetParam()),
             std::get<0>(GetParam()),
             false, /* depthwise */
             4);
}

static inline std::string
Conv2DTestCaseName(
   const testing::TestParamInfo<std::tuple<bool, bool, int, int, int, int, int>> &info)
{
   std::string name = "";

   name += "input_size_" + std::to_string(std::get<6>(info.param));
   name += "_weight_size_" + std::to_string(std::get<5>(info.param));
   name += "_input_channels_" + std::to_string(std::get<4>(info.param));
   name += "_output_channels_" + std::to_string(std::get<3>(info.param));
   name += "_stride_" + std::to_string(std::get<2>(info.param));
   name += "_padding_same_" + std::to_string(std::get<1>(info.param));
   name += "_is_signed_" + std::to_string(std::get<0>(info.param));

   return name;
}

INSTANTIATE_TEST_SUITE_P(
   , Conv2D,
   ::testing::Combine(::testing::ValuesIn(is_signed),
                      ::testing::ValuesIn(padding_same),
                      ::testing::ValuesIn(stride),
                      ::testing::ValuesIn(output_channels),
                      ::testing::ValuesIn(input_channels),
                      ::testing::ValuesIn(weight_size),
                      ::testing::ValuesIn(input_size)),
   Conv2DTestCaseName);

#endif

#if TEST_DEPTHWISE

class DepthwiseConv2D : public testing::TestWithParam<std::tuple<bool, bool, int, int, int, int>> {};

TEST_P(DepthwiseConv2D, Op)
{
   test_conv(std::get<5>(GetParam()),
             std::get<4>(GetParam()),
             std::get<3>(GetParam()),
             std::get<3>(GetParam()),
             std::get<2>(GetParam()),
             std::get<1>(GetParam()),
             std::get<0>(GetParam()),
             true, /* depthwise */
             4);
}

static inline std::string
DepthwiseConv2DTestCaseName(
   const testing::TestParamInfo<std::tuple<bool, bool, int, int, int, int>> &info)
{
   std::string name = "";

   name += "input_size_" + std::to_string(std::get<5>(info.param));
   name += "_weight_size_" + std::to_string(std::get<4>(info.param));
   name += "_channels_" + std::to_string(std::get<3>(info.param));
   name += "_stride_" + std::to_string(std::get<2>(info.param));
   name += "_padding_same_" + std::to_string(std::get<1>(info.param));
   name += "_is_signed_" + std::to_string(std::get<0>(info.param));

   return name;
}

INSTANTIATE_TEST_SUITE_P(
   , DepthwiseConv2D,
   ::testing::Combine(::testing::ValuesIn(is_signed),
                      ::testing::ValuesIn(padding_same),
                      ::testing::ValuesIn(stride),
                      ::testing::ValuesIn(dw_channels),
                      ::testing::ValuesIn(dw_weight_size),
                      ::testing::ValuesIn(input_size)),
   DepthwiseConv2DTestCaseName);

#endif

#if TEST_ADD

class Add : public testing::TestWithParam<std::tuple<bool, bool, int, int, int, int, int>> {};

TEST_P(Add, Op)
{
   test_add(std::get<6>(GetParam()),
            std::get<5>(GetParam()),
            std::get<4>(GetParam()),
            std::get<3>(GetParam()),
            std::get<2>(GetParam()),
            std::get<1>(GetParam()),
            std::get<0>(GetParam()),
            false, /* depthwise */
            4,
            TOLERANCE);
}

static inline std::string
AddTestCaseName(
   const testing::TestParamInfo<std::tuple<bool, bool, int, int, int, int, int>> &info)
{
   std::string name = "";

   name += "input_size_" + std::to_string(std::get<6>(info.param));
   name += "_weight_size_" + std::to_string(std::get<5>(info.param));
   name += "_input_channels_" + std::to_string(std::get<4>(info.param));
   name += "_output_channels_" + std::to_string(std::get<3>(info.param));
   name += "_stride_" + std::to_string(std::get<2>(info.param));
   name += "_padding_same_" + std::to_string(std::get<1>(info.param));
   name += "_is_signed_" + std::to_string(std::get<0>(info.param));

   return name;
}

INSTANTIATE_TEST_SUITE_P(
   , Add,
   ::testing::Combine(::testing::ValuesIn(is_signed),
                      ::testing::ValuesIn(padding_same),
                      ::testing::ValuesIn(stride),
                      ::testing::ValuesIn(output_channels),
                      ::testing::ValuesIn(input_channels),
                      ::testing::ValuesIn(weight_size),
                      ::testing::ValuesIn(input_size)),
   AddTestCaseName);

class AddQuant : public testing::TestWithParam<int> {};

TEST_P(AddQuant, Op)
{
   test_add(40,
            1,
            1,
            1,
            1,
            false, /* padding_same */
            false, /* is_signed */
            false, /* depthwise */
            GetParam(),
            QUANT_TOLERANCE);
}

INSTANTIATE_TEST_SUITE_P(
   , AddQuant,
   ::testing::Range(0, 100));

#endif

#if TEST_FULLY_CONNECTED

class FullyConnected : public testing::TestWithParam<std::tuple<bool, int, int>> {};

TEST_P(FullyConnected, Op)
{
   test_fully_connected(
             std::get<2>(GetParam()),
             std::get<1>(GetParam()),
             std::get<0>(GetParam()),
             4);
}

static inline std::string
FullyConnectedTestCaseName(
   const testing::TestParamInfo<std::tuple<bool, int, int>> &info)
{
   std::string name = "";

   name += "input_size_" + std::to_string(std::get<2>(info.param));
   name += "_output_channels_" + std::to_string(std::get<1>(info.param));
   name += "_is_signed_" + std::to_string(std::get<0>(info.param));

   return name;
}

INSTANTIATE_TEST_SUITE_P(
   , FullyConnected,
   ::testing::Combine(::testing::ValuesIn(is_signed),
                      ::testing::ValuesIn(output_channels),
                      ::testing::ValuesIn(fc_size)),
   FullyConnectedTestCaseName);

#endif

#if TEST_MOBILENETV1

class MobileNetV1 : public ::testing::Test {};

class MobileNetV1Param : public testing::TestWithParam<int> {};

TEST(MobileNetV1, Whole)
{
   std::ostringstream file_path;
   assert(getenv("TEFLON_TEST_DATA"));
   file_path << getenv("TEFLON_TEST_DATA") << "/mobilenet_v1_1.0_224_quant.tflite";

   test_model_file(file_path.str(), MODEL_TOLERANCE, true);
}

TEST_P(MobileNetV1Param, Op)
{
   std::ostringstream file_path;
   assert(getenv("TEFLON_TEST_DATA"));
   file_path << getenv("TEFLON_TEST_DATA") << "/mb-" << std::setfill('0') << std::setw(3) << GetParam() << ".tflite";

   test_model_file(file_path.str(), MODEL_TOLERANCE, true);
}

static inline std::string
MobileNetV1TestCaseName(
   const testing::TestParamInfo<int> &info)
{
   std::string name = "";
   std::string param = std::to_string(info.param);

   name += "mb";
   name += std::string(3 - param.length(), '0');
   name += param;

   return name;
}

INSTANTIATE_TEST_SUITE_P(
   , MobileNetV1Param,
   ::testing::Range(0, 31),
   MobileNetV1TestCaseName);

#endif

#if TEST_MOBILEDET

class MobileDet : public ::testing::Test {};

class MobileDetParam : public testing::TestWithParam<int> {};

TEST(MobileDet, Whole)
{
   std::ostringstream file_path;
   assert(getenv("TEFLON_TEST_DATA"));
   file_path << getenv("TEFLON_TEST_DATA") << "/ssdlite_mobiledet_coco_qat_postprocess.tflite";

   test_model_file(file_path.str(), MODEL_TOLERANCE, true);
}

TEST_P(MobileDetParam, Op)
{
   std::ostringstream file_path;
   assert(getenv("TEFLON_TEST_DATA"));
   file_path << getenv("TEFLON_TEST_DATA") << "/mobiledet-" << std::setfill('0') << std::setw(3) << GetParam() << ".tflite";

   test_model_file(file_path.str(), MODEL_TOLERANCE, true);
}

static inline std::string
MobileDetTestCaseName(
   const testing::TestParamInfo<int> &info)
{
   std::string name = "";
   std::string param = std::to_string(info.param);

   name += "mobiledet";
   name += std::string(3 - param.length(), '0');
   name += param;

   return name;
}

INSTANTIATE_TEST_SUITE_P(
   , MobileDetParam,
   ::testing::Range(0, 124),
   MobileDetTestCaseName);

#endif

#if TEST_YOLOX

class YoloX : public ::testing::Test {};

class YoloXParam : public testing::TestWithParam<int> {};

TEST(YoloX, Whole)
{
   std::ostringstream file_path;
   assert(getenv("TEFLON_TEST_DATA"));
   file_path << getenv("TEFLON_TEST_DATA") << "/yolox.tflite";

   test_model_file(file_path.str(), YOLOX_TOLERANCE, true);
}

TEST_P(YoloXParam, Op)
{
   std::ostringstream file_path;
   assert(getenv("TEFLON_TEST_DATA"));
   file_path << getenv("TEFLON_TEST_DATA") << "/yolox-" << std::setfill('0') << std::setw(3) << GetParam() << ".tflite";

   test_model_file(file_path.str(), MODEL_TOLERANCE, true);
}

static inline std::string
YoloXTestCaseName(
   const testing::TestParamInfo<int> &info)
{
   std::string name = "";
   std::string param = std::to_string(info.param);

   name += "yolox";
   name += std::string(3 - param.length(), '0');
   name += param;

   return name;
}

INSTANTIATE_TEST_SUITE_P(
   , YoloXParam,
   ::testing::Range(0, 128),
   YoloXTestCaseName);

#endif

int
main(int argc, char **argv)
{
   if (argc > 1 && !strcmp(argv[1], "generate_model")) {
      void *buf = NULL;
      size_t buf_size;

      assert(argc == 11);

      std::cout << "Generating model to ./model.tflite\n";

      int n = 2;
      int input_size = atoi(argv[n++]);
      int weight_size = atoi(argv[n++]);
      int input_channels = atoi(argv[n++]);
      int output_channels = atoi(argv[n++]);
      int stride = atoi(argv[n++]);
      int padding_same = atoi(argv[n++]);
      int is_signed = atoi(argv[n++]);
      int depthwise = atoi(argv[n++]);
      int seed = atoi(argv[n++]);

      set_seed(seed);

      buf = conv2d_generate_model(input_size, weight_size,
                                  input_channels, output_channels,
                                  stride, padding_same, is_signed,
                                  depthwise, &buf_size);

      int fd = open("model.tflite", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      write(fd, buf, buf_size);
      close(fd);

      return 0;
   } else if (argc > 1 && !strcmp(argv[1], "run_model")) {
      test_model_file(std::string(argv[2]), MODEL_TOLERANCE, false);
   } else {
      testing::InitGoogleTest(&argc, argv);
      return RUN_ALL_TESTS();
   }
}
