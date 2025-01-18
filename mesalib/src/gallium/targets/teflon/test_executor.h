/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <cstddef>
enum executor {
   EXECUTOR_CPU,
   EXECUTOR_NPU,
};

struct TfLiteModel;

void *conv2d_generate_model(int input_size,
                            int weight_size,
                            int input_channels,
                            int output_channels,
                            int stride,
                            bool padding_same,
                            bool is_signed,
                            bool depthwise,
                            size_t *buf_size);

void *add_generate_model(int input_size,
                         int weight_size,
                         int input_channels,
                         int output_channels,
                         int stride,
                         bool padding_same,
                         bool is_signed,
                         bool depthwise,
                         size_t *buf_size);

void *fully_connected_generate_model(int input_size,
                                     int output_channels,
                                     bool is_signed,
                                     size_t *buf_size);

void run_model(TfLiteModel *model, enum executor executor, void ***input, size_t *num_inputs,
               void ***output, size_t **output_sizes, TfLiteType **output_types,
               size_t *num_outputs, std::string cache_dir);

bool cache_is_enabled(void);

void *read_buf(const char *path, size_t *buf_size);
