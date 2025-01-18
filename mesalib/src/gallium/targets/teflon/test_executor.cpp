/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <stdio.h>
#include <vector>
#include <gtest/gtest.h>
#include <xtensor/xrandom.hpp>

#include "util/macros.h"

#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/c/common.h"

#include <fcntl.h>
#include "test_executor.h"
#include "tflite-schema-v2.15.0_generated.h"

static float
randf(float min, float max)
{
   return ((max - min) * ((float)rand() / (float)RAND_MAX)) + min;
}

static void
read_model(const char *file_name, tflite::ModelT &model)
{
   std::ostringstream file_path;
   assert(getenv("TEFLON_TEST_DATA"));
   file_path << getenv("TEFLON_TEST_DATA") << "/" << file_name;

   FILE *f = fopen(file_path.str().c_str(), "rb");
   assert(f);
   fseek(f, 0, SEEK_END);
   long fsize = ftell(f);
   fseek(f, 0, SEEK_SET);
   void *buf = malloc(fsize);
   fread(buf, fsize, 1, f);
   fclose(f);

   tflite::GetModel(buf)->UnPackTo(&model);
}

static void
patch_conv2d(unsigned operation_index,
             tflite::ModelT *model,
             int input_size,
             int weight_size,
             int input_channels,
             int output_channels,
             int stride,
             bool padding_same,
             bool is_signed,
             bool depthwise)
{
   unsigned output_size = 0;
   unsigned input_index;
   unsigned weights_index;
   unsigned bias_index;
   unsigned output_index;
   unsigned weights_buffer_index;
   unsigned bias_buffer_index;

   auto subgraph = model->subgraphs[0];

   /* Operation */
   if (depthwise) {
      auto value = new tflite::DepthwiseConv2DOptionsT();
      value->depth_multiplier = 1;
      value->padding = padding_same ? tflite::Padding_SAME : tflite::Padding_VALID;
      value->stride_w = stride;
      value->stride_h = stride;
      value->dilation_w_factor = 1;
      value->dilation_h_factor = 1;
      subgraph->operators[operation_index]->builtin_options.value = value;
      subgraph->operators[operation_index]->builtin_options.type = tflite::BuiltinOptions_DepthwiseConv2DOptions;

      model->operator_codes[0]->deprecated_builtin_code = 4;
      model->operator_codes[0]->builtin_code = tflite::BuiltinOperator_DEPTHWISE_CONV_2D;
   } else {
      auto value = new tflite::Conv2DOptionsT();
      value->padding = padding_same ? tflite::Padding_SAME : tflite::Padding_VALID;
      value->stride_w = stride;
      value->stride_h = stride;
      subgraph->operators[operation_index]->builtin_options.value = value;
   }

   input_index = subgraph->operators[operation_index]->inputs.data()[0];
   weights_index = subgraph->operators[operation_index]->inputs.data()[1];
   bias_index = subgraph->operators[operation_index]->inputs.data()[2];
   output_index = subgraph->operators[operation_index]->outputs.data()[0];

   /* Input */
   auto input_tensor = subgraph->tensors[input_index];
   input_tensor->shape.data()[0] = 1;
   input_tensor->shape.data()[1] = input_size;
   input_tensor->shape.data()[2] = input_size;
   input_tensor->shape.data()[3] = input_channels;
   input_tensor->type = is_signed ? tflite::TensorType_INT8 : tflite::TensorType_UINT8;
   if (is_signed)
      input_tensor->quantization->zero_point[0] -= 128;

   /* Bias */
   auto bias_tensor = subgraph->tensors[bias_index];
   bias_buffer_index = bias_tensor->buffer;
   bias_tensor->shape.data()[0] = output_channels;

   auto bias_data = &model->buffers[bias_buffer_index]->data;
   xt::xarray<int32_t> bias_array = xt::random::randint<int32_t>({output_channels}, -20000, 20000);
   bias_data->resize(bias_array.size() * sizeof(int32_t));
   memcpy(bias_data->data(), bias_array.data(), bias_array.size() * sizeof(int32_t));

   /* Weight */
   auto weight_tensor = subgraph->tensors[weights_index];
   weights_buffer_index = weight_tensor->buffer;
   if (depthwise) {
      weight_tensor->shape.data()[0] = 1;
      weight_tensor->shape.data()[1] = weight_size;
      weight_tensor->shape.data()[2] = weight_size;
      weight_tensor->shape.data()[3] = output_channels;
   } else {
      weight_tensor->shape.data()[0] = output_channels;
      weight_tensor->shape.data()[1] = weight_size;
      weight_tensor->shape.data()[2] = weight_size;
      weight_tensor->shape.data()[3] = input_channels;
   }
   weight_tensor->type = is_signed ? tflite::TensorType_INT8 : tflite::TensorType_UINT8;
   if (is_signed)
      weight_tensor->quantization->zero_point[0] = 0;

   auto weights_data = &model->buffers[weights_buffer_index]->data;
   std::vector<int> weight_shape;
   if (depthwise)
      weight_shape = {1, weight_size, weight_size, output_channels};
   else
      weight_shape = {output_channels, weight_size, weight_size, input_channels};

   xt::xarray<uint8_t> weights_array = xt::random::randint<uint8_t>(weight_shape, 0, 255);
   weights_data->resize(weights_array.size());
   memcpy(weights_data->data(), weights_array.data(), weights_array.size());

   /* Output */
   if (padding_same)
      output_size = (input_size + stride - 1) / stride;
   else
      output_size = (input_size + stride - weight_size) / stride;

   auto output_tensor = subgraph->tensors[output_index];
   output_tensor->shape.data()[0] = 1;
   output_tensor->shape.data()[1] = output_size;
   output_tensor->shape.data()[2] = output_size;
   output_tensor->shape.data()[3] = output_channels;
   output_tensor->type = is_signed ? tflite::TensorType_INT8 : tflite::TensorType_UINT8;
   if (is_signed)
      output_tensor->quantization->zero_point[0] -= 128;
}

void *
conv2d_generate_model(int input_size,
                      int weight_size,
                      int input_channels,
                      int output_channels,
                      int stride,
                      bool padding_same,
                      bool is_signed,
                      bool depthwise,
                      size_t *buf_size)
{
   void *buf;
   tflite::ModelT model;
   read_model("conv2d.tflite", model);

   patch_conv2d(0, &model, input_size, weight_size, input_channels, output_channels, stride, padding_same, is_signed, depthwise);

   flatbuffers::FlatBufferBuilder builder;
   builder.Finish(tflite::Model::Pack(builder, &model), "TFL3");

   *buf_size = builder.GetSize();
   buf = malloc(*buf_size);
   memcpy(buf, builder.GetBufferPointer(), builder.GetSize());

   return buf;
}

static void
patch_quant_for_add(tflite::ModelT *model, bool is_signed)
{
   auto subgraph = model->subgraphs[0];
   auto add_op = subgraph->operators[2];

   auto input_index = add_op->inputs.data()[0];
   auto input_tensor = subgraph->tensors[input_index];
   input_tensor->quantization->scale[0] = randf(0.0078125, 0.4386410117149353);
   input_tensor->quantization->zero_point[0] = rand() % 255;
   if (is_signed)
      input_tensor->quantization->zero_point[0] -= 128;

   input_index = add_op->inputs.data()[1];
   input_tensor = subgraph->tensors[input_index];
   input_tensor->quantization->scale[0] = randf(0.0078125, 0.4386410117149353);
   input_tensor->quantization->zero_point[0] = rand() % 255;
   if (is_signed)
      input_tensor->quantization->zero_point[0] -= 128;
}

void *
add_generate_model(int input_size,
                   int weight_size,
                   int input_channels,
                   int output_channels,
                   int stride,
                   bool padding_same,
                   bool is_signed,
                   bool depthwise,
                   size_t *buf_size)
{
   void *buf;
   tflite::ModelT model;
   read_model("add.tflite", model);

   patch_conv2d(0, &model, input_size, weight_size, input_channels, output_channels, stride, padding_same, is_signed, depthwise);
   patch_conv2d(1, &model, input_size, weight_size, input_channels, output_channels, stride, padding_same, is_signed, depthwise);
   patch_quant_for_add(&model, is_signed);

   /* Output */
   auto subgraph = model.subgraphs[0];
   unsigned input_index = subgraph->operators[2]->inputs.data()[0];
   unsigned output_index = subgraph->operators[2]->outputs.data()[0];

   auto input_tensor = subgraph->tensors[input_index];
   auto output_tensor = subgraph->tensors[output_index];
   output_tensor->shape.data()[0] = input_tensor->shape.data()[0];
   output_tensor->shape.data()[1] = input_tensor->shape.data()[1];
   output_tensor->shape.data()[2] = input_tensor->shape.data()[2];
   output_tensor->shape.data()[3] = input_tensor->shape.data()[3];
   output_tensor->type = is_signed ? tflite::TensorType_INT8 : tflite::TensorType_UINT8;

   flatbuffers::FlatBufferBuilder builder;
   builder.Finish(tflite::Model::Pack(builder, &model), "TFL3");

   *buf_size = builder.GetSize();
   buf = malloc(*buf_size);
   memcpy(buf, builder.GetBufferPointer(), builder.GetSize());

   return buf;
}

static void
patch_fully_connected(unsigned operation_index,
                      tflite::ModelT *model,
                      int input_size,
                      int output_channels,
                      bool is_signed)
{
   unsigned input_index;
   unsigned weights_index;
   unsigned bias_index;
   unsigned output_index;
   unsigned weights_buffer_index;
   unsigned bias_buffer_index;

   auto subgraph = model->subgraphs[0];

   /* Operation */
   auto value = new tflite::FullyConnectedOptionsT();
   subgraph->operators[operation_index]->builtin_options.value = value;

   input_index = subgraph->operators[operation_index]->inputs.data()[0];
   weights_index = subgraph->operators[operation_index]->inputs.data()[1];
   bias_index = subgraph->operators[operation_index]->inputs.data()[2];
   output_index = subgraph->operators[operation_index]->outputs.data()[0];

   /* Input */
   auto input_tensor = subgraph->tensors[input_index];
   input_tensor->shape.data()[0] = 1;
   input_tensor->shape.data()[1] = input_size;
   input_tensor->type = is_signed ? tflite::TensorType_INT8 : tflite::TensorType_UINT8;

   /* Bias */
   auto bias_tensor = subgraph->tensors[bias_index];
   bias_buffer_index = bias_tensor->buffer;
   bias_tensor->shape.data()[0] = output_channels;

   auto bias_data = &model->buffers[bias_buffer_index]->data;
   xt::xarray<int32_t> bias_array = xt::random::randint<int32_t>({output_channels}, -20000, 20000);
   bias_data->resize(bias_array.size() * sizeof(int32_t));
   memcpy(bias_data->data(), bias_array.data(), bias_array.size() * sizeof(int32_t));

   /* Weight */
   auto weight_tensor = subgraph->tensors[weights_index];
   weights_buffer_index = weight_tensor->buffer;
   weight_tensor->shape.data()[0] = output_channels;
   weight_tensor->shape.data()[1] = input_size;
   weight_tensor->type = is_signed ? tflite::TensorType_INT8 : tflite::TensorType_UINT8;

   auto weights_data = &model->buffers[weights_buffer_index]->data;
   std::vector<int> weight_shape;
   weight_shape = {output_channels, input_size};

   xt::xarray<uint8_t> weights_array = xt::random::randint<uint8_t>(weight_shape, 0, 255);
   weights_data->resize(weights_array.size());
   memcpy(weights_data->data(), weights_array.data(), weights_array.size());

   /* Output */
   auto output_tensor = subgraph->tensors[output_index];
   output_tensor->shape.data()[0] = 1;
   output_tensor->shape.data()[1] = output_channels;
   output_tensor->type = is_signed ? tflite::TensorType_INT8 : tflite::TensorType_UINT8;
}

void *
fully_connected_generate_model(int input_size,
                               int output_channels,
                               bool is_signed,
                               size_t *buf_size)
{
   void *buf;
   tflite::ModelT model;
   read_model("fully_connected.tflite", model);

   patch_fully_connected(0, &model, input_size, output_channels, is_signed);

   flatbuffers::FlatBufferBuilder builder;
   builder.Finish(tflite::Model::Pack(builder, &model), "TFL3");

   *buf_size = builder.GetSize();
   buf = malloc(*buf_size);
   memcpy(buf, builder.GetBufferPointer(), builder.GetSize());

   return buf;
}

static void
tflite_error_cb(void *user_data, const char *format, va_list args)
{
   vfprintf(stderr, format, args);
}

TfLiteDelegate *(*tflite_plugin_create_delegate)(char **options_keys,
                                                 char **options_values,
                                                 size_t num_options,
                                                 void (*report_error)(const char *));

void (*tflite_plugin_destroy_delegate)(TfLiteDelegate *delegate);

static void
load_delegate()
{
   const char *delegate_path = getenv("TEFLON_TEST_DELEGATE");
   assert(delegate_path);

   void *delegate_lib = dlopen(delegate_path, RTLD_LAZY | RTLD_LOCAL);
   assert(delegate_lib);

   tflite_plugin_create_delegate = reinterpret_cast<TfLiteDelegate *(*)(char **options_keys,
                                                                        char **options_values,
                                                                        size_t num_options,
                                                                        void (*report_error)(const char *))>(
      dlsym(delegate_lib, "tflite_plugin_create_delegate"));
   assert(tflite_plugin_create_delegate);

   tflite_plugin_destroy_delegate = reinterpret_cast<void (*)(TfLiteDelegate *delegate)>(
      dlsym(delegate_lib, "tflite_plugin_destroy_delegate"));
   assert(tflite_plugin_destroy_delegate);
}

bool
cache_is_enabled(void)
{
   return getenv("TEFLON_ENABLE_CACHE");
}

void *
read_buf(const char *path, size_t *buf_size)
{
   FILE *f = fopen(path, "rb");
   if (f == NULL)
      return NULL;

   fseek(f, 0, SEEK_END);
   long fsize = ftell(f);
   fseek(f, 0, SEEK_SET);

   void *buf = malloc(fsize);
   fread(buf, fsize, 1, f);

   fclose(f);

   if(buf_size != NULL)
      *buf_size = fsize;

   return buf;
}

void
run_model(TfLiteModel *model, enum executor executor, void ***input, size_t *num_inputs,
          void ***output, size_t **output_sizes, TfLiteType **output_types,
          size_t *num_outputs, std::string cache_dir)
{
   TfLiteDelegate *delegate = NULL;
   TfLiteInterpreterOptions *options = TfLiteInterpreterOptionsCreate();

   if (executor == EXECUTOR_NPU) {
      load_delegate();
      delegate = tflite_plugin_create_delegate(NULL, NULL, 0, NULL);
      TfLiteInterpreterOptionsAddDelegate(options, delegate);
   }

   TfLiteInterpreterOptionsSetErrorReporter(options, tflite_error_cb, NULL);

   TfLiteInterpreter *interpreter = TfLiteInterpreterCreate(model, options);
   assert(interpreter);

   TfLiteInterpreterAllocateTensors(interpreter);

   *num_inputs = TfLiteInterpreterGetInputTensorCount(interpreter);
   if (*input == NULL)
      *input = (void**)calloc(*num_inputs, sizeof(*input));
   for (unsigned i = 0; i < *num_inputs; i++) {
      TfLiteTensor *input_tensor = TfLiteInterpreterGetInputTensor(interpreter, i);
      std::ostringstream input_cache;
      input_cache << cache_dir << "/" << "input-" << i << ".data";

      if ((*input)[i] == NULL) {
         if (cache_is_enabled())
            (*input)[i] = read_buf(input_cache.str().c_str(), NULL);
         if ((*input)[i] == NULL) {
            (*input)[i] = malloc(input_tensor->bytes);

            std::vector<size_t> shape;

            shape.resize(input_tensor->dims->size);
            for (int j = 0; j < input_tensor->dims->size; j++)
               shape[j] = input_tensor->dims->data[j];

            switch (input_tensor->type) {
               case kTfLiteFloat32: {
                  xt::xarray<float_t> a = xt::random::rand<float_t>(shape);
                  memcpy((*input)[i], a.data(), input_tensor->bytes);
                  break;
               }
               default: {
                  xt::xarray<uint8_t> a = xt::random::randint<uint8_t>(shape, 0, 255);
                  memcpy((*input)[i], a.data(), input_tensor->bytes);
                  break;
               }
            }

            if (cache_is_enabled()) {
               if (!cache_dir.empty() && !std::filesystem::exists(cache_dir))
                  std::filesystem::create_directory(cache_dir);

               std::ofstream file(input_cache.str().c_str(), std::ios::out | std::ios::binary);
               file.write(reinterpret_cast<const char *>((*input)[i]), input_tensor->bytes);
               file.close();
            }
         }
      }

      TfLiteTensorCopyFromBuffer(input_tensor, (*input)[i], input_tensor->bytes);
   }

   std::ostringstream output_cache;
   output_cache << cache_dir << "/" << "output-" << 0 << ".data";

   if (executor == EXECUTOR_NPU || !cache_is_enabled() || !std::filesystem::exists(output_cache.str())) {
      EXPECT_EQ(TfLiteInterpreterInvoke(interpreter), kTfLiteOk);
   }

   *num_outputs = TfLiteInterpreterGetOutputTensorCount(interpreter);
   *output = (void**)malloc(sizeof(*output) * *num_outputs);
   *output_sizes = (size_t*)malloc(sizeof(*output_sizes) * *num_outputs);
   *output_types = (TfLiteType*)malloc(sizeof(*output_types) * *num_outputs);
   for (unsigned i = 0; i < *num_outputs; i++) {
      const TfLiteTensor *output_tensor = TfLiteInterpreterGetOutputTensor(interpreter, i);
      output_cache.str("");
      output_cache << cache_dir << "/" << "output-" << i << ".data";
      (*output_types)[i] = output_tensor->type;

      if (executor == EXECUTOR_CPU && cache_is_enabled() && std::filesystem::exists(output_cache.str())) {
         (*output)[i] = read_buf(output_cache.str().c_str(), NULL);
      } else {
         (*output)[i] = malloc(output_tensor->bytes);
         EXPECT_EQ(TfLiteTensorCopyToBuffer(output_tensor, (*output)[i], output_tensor->bytes), kTfLiteOk);

         if (cache_is_enabled() && executor == EXECUTOR_CPU) {
            std::ofstream file = std::ofstream(output_cache.str().c_str(), std::ios::out | std::ios::binary);
            file.write(reinterpret_cast<const char *>((*output)[i]), output_tensor->bytes);
            file.close();
         }
      }

      switch (output_tensor->type) {
         case kTfLiteFloat32: {
            (*output_sizes)[i] = output_tensor->bytes / 4;
            break;
         }
         default: {
            (*output_sizes)[i] = output_tensor->bytes;
            break;
         }
      }
   }

   TfLiteInterpreterDelete(interpreter);
   if (executor == EXECUTOR_NPU)
      tflite_plugin_destroy_delegate(delegate);
   TfLiteInterpreterOptionsDelete(options);
}
