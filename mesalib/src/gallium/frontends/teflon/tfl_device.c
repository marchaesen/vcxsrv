/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "pipe-loader/pipe_loader.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/builtin_ops.h"
#include "tensorflow/lite/core/c/builtin_op_data.h"

/* TODO: Move to TfLiteAsyncKernel for zero-copy of buffers */

enum teflon_debug_flags {
   TEFLON_DEBUG_VERBOSE = 1 << 1,
};

static const struct debug_named_value teflon_debug_flags[] = {
    { "verbose", TEFLON_DEBUG_VERBOSE, "Verbose logging." },
    DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(debug_teflon, "TEFLON_DEBUG", teflon_debug_flags, 0)

static inline void
teflon_debug(const char *format, ...)
{
   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      va_list ap;
      va_start(ap, format);
      _debug_vprintf(format, ap);
      va_end(ap);
   }
}

struct teflon_delegate
{
   TfLiteDelegate base;
   struct pipe_loader_device *dev;
   struct pipe_context *context;
};

struct teflon_subgraph
{
   struct pipe_ml_subgraph *base;

   unsigned *input_tensors;
   unsigned input_count;

   unsigned *output_tensors;
   unsigned output_count;
};

static struct pipe_resource *
create_resource(struct pipe_context *context, TfLiteTensor tensor)
{
   unsigned bytes;
   unsigned size = 1;

   for (int i = 0; i < tensor.dims->size; i++)
      size *= tensor.dims->data[i];

   switch(tensor.type) {
      case kTfLiteInt8:
      case kTfLiteUInt8:
         bytes = 1;
         break;
      case kTfLiteInt16:
      case kTfLiteUInt16:
      case kTfLiteFloat16:
         bytes = 2;
         break;
      case kTfLiteInt32:
      case kTfLiteUInt32:
      case kTfLiteFloat32:
         bytes = 4;
         break;
      case kTfLiteInt64:
      case kTfLiteUInt64:
      case kTfLiteFloat64:
      case kTfLiteComplex64:
         bytes = 8;
         break;
      default:
         unreachable("Unsupported TF type");
   }

   return pipe_buffer_create_with_data(context, 0, PIPE_USAGE_DEFAULT, size * bytes, tensor.data.data);
}

static void
fill_operation(struct teflon_delegate *delegate, TfLiteContext *tf_context, TfLiteNode *node, TfLiteRegistration *node_registration, struct pipe_ml_operation *operation, struct pipe_tensor *tensors)
{
   operation->input_count = node->inputs->size;
   operation->input_tensors = calloc(operation->input_count, sizeof(void*));
   for (unsigned i = 0; i < node->inputs->size; i++)
      operation->input_tensors[i] = &tensors[node->inputs->data[i]];

   operation->output_count = node->outputs->size;
   operation->output_tensors = calloc(operation->output_count, sizeof(void*));
   for (unsigned i = 0; i < node->outputs->size; i++)
      operation->output_tensors[i] = &tensors[node->outputs->data[i]];

   switch(node_registration->builtin_code) {
      case kTfLiteBuiltinConv2d:
      case kTfLiteBuiltinDepthwiseConv2d: {
         operation->type = PIPE_ML_OPERATION_TYPE_CONVOLUTION;
         operation->conv.weight_tensor = &tensors[node->inputs->data[1]];
         operation->conv.bias_tensor = &tensors[node->inputs->data[2]];
         if (node_registration->builtin_code == kTfLiteBuiltinConv2d) {
            TfLiteConvParams* params = (TfLiteConvParams*)node->builtin_data;

            assert(params->activation == kTfLiteActNone ||
                   params->activation == kTfLiteActRelu ||
                   params->activation == kTfLiteActRelu6);
            if (node_registration->version >= 2) {
               assert(params->dilation_width_factor == 1);
               assert(params->dilation_height_factor == 1);
            }
            operation->conv.stride_x = params->stride_width;
            operation->conv.stride_y = params->stride_height;
            operation->conv.padding_same = params->padding == kTfLitePaddingSame;
            operation->conv.depthwise = false;
            operation->conv.relu = params->activation == kTfLiteActRelu ||
                                   params->activation == kTfLiteActRelu6;
         } else {
            TfLiteDepthwiseConvParams* params = (TfLiteDepthwiseConvParams*)node->builtin_data;

            assert(params->activation == kTfLiteActNone ||
                   params->activation == kTfLiteActRelu ||
                   params->activation == kTfLiteActRelu6);
            if (node_registration->version >= 2) {
               assert(params->dilation_width_factor == 1);
               assert(params->dilation_height_factor == 1);
            }
            operation->conv.stride_x = params->stride_width;
            operation->conv.stride_y = params->stride_height;
            operation->conv.padding_same = params->padding == kTfLitePaddingSame;
            operation->conv.depthwise = true;
            operation->conv.relu = params->activation == kTfLiteActRelu ||
                                   params->activation == kTfLiteActRelu6;
         }
         operation->conv.pointwise = operation->conv.weight_tensor->dims[1] == 1 && \
                                     operation->conv.weight_tensor->dims[2] == 1;
         break;
      }
      case kTfLiteBuiltinAveragePool2d:
         operation->type = PIPE_ML_OPERATION_TYPE_POOLING;
         break;
      case kTfLiteBuiltinAdd:
         operation->type = PIPE_ML_OPERATION_TYPE_ADD;
         break;
      case kTfLiteBuiltinConcatenation:
         operation->type = PIPE_ML_OPERATION_TYPE_CONCATENATION;
         break;
      case kTfLiteBuiltinSplit:
         operation->type = PIPE_ML_OPERATION_TYPE_SPLIT;
         break;
      case kTfLiteBuiltinPad: {
         int32_t *paddings = tf_context->tensors[node->inputs->data[1]].data.data;

         operation->type = PIPE_ML_OPERATION_TYPE_PAD;
         operation->pad.before_x = paddings[2];
         operation->pad.after_x = paddings[3];
         operation->pad.before_y = paddings[4];
         operation->pad.after_y = paddings[5];
         break;
      }
      case kTfLiteBuiltinFullyConnected: {
         operation->type = PIPE_ML_OPERATION_TYPE_FULLY_CONNECTED;
         operation->fcon.weight_tensor = &tensors[node->inputs->data[1]];
         operation->fcon.bias_tensor = &tensors[node->inputs->data[2]];
         break;
      }
      default:
         unreachable("Unsupported ML operation type");
   }
}

static void
fill_tensor(struct teflon_delegate *delegate, TfLiteContext *tf_context, struct pipe_tensor *tensor, unsigned index)
{
   struct pipe_context *context = delegate->context;
   TfLiteTensor tf_tensor = tf_context->tensors[index];

   if (tf_tensor.type == kTfLiteNoType)
      return; /* Placeholder tensor */

   if (tf_tensor.data.data)
      tensor->resource = create_resource(context, tf_tensor);

   tensor->index = index;
   memcpy(tensor->dims, tf_tensor.dims->data, tf_tensor.dims->size * sizeof(*tensor->dims));

   if (tf_tensor.quantization.type == kTfLiteAffineQuantization) {
      const TfLiteAffineQuantization *quant = (const TfLiteAffineQuantization *)tf_tensor.quantization.params;
      tensor->scale = quant->scale->data[0];
      tensor->zero_point = quant->zero_point->data[0];
   }

   switch(tf_tensor.type) {
      case kTfLiteUInt8:
      case kTfLiteUInt16:
      case kTfLiteUInt32:
      case kTfLiteUInt64:
         tensor->is_signed = false;
         break;
      default:
         tensor->is_signed = true;
   }
}

static void
dump_graph(struct pipe_tensor *tensors, unsigned tensor_count, struct pipe_ml_operation *operations, unsigned operation_count)
{
   teflon_debug("\n");
   teflon_debug("teflon: compiling graph: %d tensors %d operations\n",
                tensor_count, operation_count);

   teflon_debug("%3s %-8s %3s %s %-12s\n", "idx", "scale", "zp", "has_data", "size");
   teflon_debug("=======================================\n");
   for (int i = 0; i < tensor_count; i++) {
      teflon_debug("%3d %6f %3x %-8s %dx%dx%dx%d\n",
                  tensors[i].index,
                  tensors[i].scale,
                  tensors[i].zero_point,
                  tensors[i].resource == NULL ? "no" : "yes",
                  tensors[i].dims[0], tensors[i].dims[1], tensors[i].dims[2], tensors[i].dims[3]);
   }

   teflon_debug("\n");
   teflon_debug("%3s %-6s %25s %25s  %s\n", "idx", "type", "inputs", "outputs", "operation type-specific");
   teflon_debug("================================================================================================\n");
   for (int i = 0; i < operation_count; i++) {
      teflon_debug("%3d ", i);

      switch(operations[i].type) {
         case PIPE_ML_OPERATION_TYPE_ADD:
            teflon_debug("%-6s ", "ADD");
            break;
         case PIPE_ML_OPERATION_TYPE_CONVOLUTION:
            teflon_debug("%-6s ", operations[i].conv.depthwise ? "DWCONV" : "CONV");
            break;
         case PIPE_ML_OPERATION_TYPE_CONCATENATION:
            teflon_debug("%-6s ", "CONCAT");
            break;
         case PIPE_ML_OPERATION_TYPE_POOLING:
            teflon_debug("%-6s ", "POOL");
            break;
         case PIPE_ML_OPERATION_TYPE_SPLIT:
            teflon_debug("%-6s ", "SPLIT");
            break;
         case PIPE_ML_OPERATION_TYPE_PAD:
            teflon_debug("%-6s ", "PAD");
            break;
         case PIPE_ML_OPERATION_TYPE_FULLY_CONNECTED:
            teflon_debug("%-6s ", "FCON");
            break;
      }

      for (unsigned j = 0; j < operations[i].input_count; j++) {
         teflon_debug("%d", operations[i].input_tensors[j]->index);
         if (j < operations[i].input_count - 1)
            teflon_debug(",");
      }

      teflon_debug(" ");

      for (unsigned j = 0; j < operations[i].output_count; j++) {
         teflon_debug("%d", operations[i].output_tensors[j]->index);
         if (j < operations[i].output_count - 1)
            teflon_debug(",");
      }

      teflon_debug("\n");
   }
   teflon_debug("\n");
}

static void *
partition_init(TfLiteContext *tf_context, const char *buffer, size_t length)
{
   const TfLiteDelegateParams *params = (const TfLiteDelegateParams *)buffer;
   struct teflon_delegate *delegate = (struct teflon_delegate *)params->delegate;
   struct pipe_context *context = delegate->context;
   struct pipe_ml_operation operations[params->nodes_to_replace->size];
   struct pipe_tensor tensors[tf_context->tensors_size];
   long start = 0, end = 0;

   memset(operations, 0, sizeof(operations));
   memset(tensors, 0, sizeof(tensors));

   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      struct timespec time;
      clock_gettime(CLOCK_MONOTONIC, &time);
      start = (long)time.tv_sec * 1000 + (long)time.tv_nsec / 1000000;
   }

   for (int i = 0; i < tf_context->tensors_size; i++)
      fill_tensor(delegate, tf_context, &tensors[i], i);

   for (int i = 0; i < params->nodes_to_replace->size; i++)
   {
      const int node_index = params->nodes_to_replace->data[i];
      TfLiteNode *delegated_node = NULL;
      TfLiteRegistration *delegated_node_registration = NULL;
      tf_context->GetNodeAndRegistration(tf_context, node_index, &delegated_node,
                                         &delegated_node_registration);

      fill_operation(delegate, tf_context, delegated_node, delegated_node_registration, &operations[i], tensors);
   }

   if (debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)
      dump_graph(tensors, tf_context->tensors_size, operations, params->nodes_to_replace->size);

   struct pipe_ml_subgraph *subgraph;
   subgraph = context->ml_subgraph_create(context,
                                          operations,
                                          params->nodes_to_replace->size);

   for (int i = 0; i < tf_context->tensors_size; i++)
      pipe_resource_reference(&tensors[i].resource, NULL);

   struct teflon_subgraph *tsubgraph = calloc(1, sizeof(*tsubgraph));
   tsubgraph->base = subgraph;

   tsubgraph->input_tensors = malloc(params->input_tensors->size * sizeof(*tsubgraph->input_tensors));
   for (int i = 0; i < params->input_tensors->size; i++) {
      unsigned tensor_idx = params->input_tensors->data[i];
      TfLiteTensor *tensor = &tf_context->tensors[tensor_idx];
      if (tensor->allocation_type == kTfLiteMmapRo)
         continue;
      tsubgraph->input_tensors[tsubgraph->input_count] = tensor_idx;
      tsubgraph->input_count++;
   }

   tsubgraph->output_count = params->output_tensors->size;
   tsubgraph->output_tensors = malloc(params->output_tensors->size * sizeof(*tsubgraph->output_tensors));
   memcpy(tsubgraph->output_tensors, params->output_tensors->data,
          params->output_tensors->size * sizeof(*tsubgraph->output_tensors));

   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      struct timespec time;
      clock_gettime(CLOCK_MONOTONIC, &time);
      end = (long)time.tv_sec * 1000 + (long)time.tv_nsec / 1000000;
      teflon_debug("teflon: compiled graph, took %ld ms\n", (end - start));
   }

   return tsubgraph;
}

static TfLiteStatus
partition_prepare(TfLiteContext *context, TfLiteNode *node)
{
   // TODO: If input size has changed, resize input, intermediate and output buffers

   return kTfLiteOk;
}

// De-allocates the per-node-and-Interpreter custom data.
static void
partition_free(TfLiteContext *tf_context, void *buffer)
{
   struct teflon_subgraph *tsubgraph = (struct teflon_subgraph *)buffer;
   struct pipe_ml_subgraph *subgraph = tsubgraph->base;
   struct pipe_context *context = subgraph->context;

   context->ml_subgraph_destroy(context, subgraph);
   free(tsubgraph->input_tensors);
   free(tsubgraph->output_tensors);
   free(tsubgraph);
}

static TfLiteStatus
partition_invoke(TfLiteContext *tf_context, TfLiteNode *node)
{
   struct teflon_delegate *delegate = (struct teflon_delegate *)node->delegate;
   struct teflon_subgraph *tsubgraph = (struct teflon_subgraph *)node->user_data;
   struct pipe_ml_subgraph *subgraph = tsubgraph->base;
   struct pipe_context *context = delegate->context;
   long start = 0, end = 0;

   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      struct timespec time;
      clock_gettime(CLOCK_MONOTONIC, &time);
      start = (long)time.tv_sec * 1000 + (long)time.tv_nsec / 1000000;
   }

   void **buffers = malloc(tsubgraph->input_count * sizeof(*buffers));
   bool *is_signed = malloc(tsubgraph->input_count * sizeof(*is_signed));
   for (unsigned i = 0; i < tsubgraph->input_count; i++) {
      TfLiteTensor tf_tensor = tf_context->tensors[tsubgraph->input_tensors[i]];

      buffers[i] = tf_tensor.data.data;
      is_signed[i] = !(tf_tensor.type == kTfLiteUInt8 ||
                       tf_tensor.type == kTfLiteUInt16 ||
                       tf_tensor.type == kTfLiteUInt32 ||
                       tf_tensor.type == kTfLiteUInt64);
   }
   context->ml_subgraph_invoke(context, subgraph, tsubgraph->input_count, tsubgraph->input_tensors, buffers, is_signed);
   free(buffers);
   free(is_signed);

   buffers = malloc(tsubgraph->output_count * sizeof(*buffers));
   is_signed = malloc(tsubgraph->output_count * sizeof(*is_signed));
   for (unsigned i = 0; i < tsubgraph->output_count; i++) {
      TfLiteTensor tf_tensor = tf_context->tensors[tsubgraph->output_tensors[i]];

      buffers[i] = tf_tensor.data.data;
      is_signed[i] = !(tf_tensor.type == kTfLiteUInt8 ||
                       tf_tensor.type == kTfLiteUInt16 ||
                       tf_tensor.type == kTfLiteUInt32 ||
                       tf_tensor.type == kTfLiteUInt64);
   }
   context->ml_subgraph_read_output(context, subgraph, tsubgraph->output_count, tsubgraph->output_tensors, buffers, is_signed);
   free(buffers);
   free(is_signed);

   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      struct timespec time;
      clock_gettime(CLOCK_MONOTONIC, &time);
      end = (long)time.tv_sec * 1000 + (long)time.tv_nsec / 1000000;
      teflon_debug("teflon: invoked graph, took %ld ms\n", (end - start));
   }

   return kTfLiteOk;
}

static bool
tensor_quantization_supported(TfLiteTensor *tensor)
{
   if (tensor->quantization.type == kTfLiteAffineQuantization) {
      TfLiteAffineQuantization *affine = (TfLiteAffineQuantization *)tensor->quantization.params;

      /*
       * Per-axis quantization not supported, for details see:
       * https://ai.google.dev/edge/litert/models/quantization_spec#per-axis_vs_per-tensor
       */
      return affine->scale->size == 1 && affine->zero_point->size == 1;
   }
   return false;
}

static bool
fused_relu6_supported(TfLiteTensor *tensor)
{
   TfLiteAffineQuantization *affine;
   int quantized_max;

   switch (tensor->type) {
      case kTfLiteInt8:
         quantized_max = INT8_MAX;
         break;
      case kTfLiteUInt8:
         quantized_max = UINT8_MAX;
         break;
      default:
         return false;
   }

   assert(tensor->quantization.type == kTfLiteAffineQuantization);
   affine = (TfLiteAffineQuantization *)tensor->quantization.params;

   assert(affine->scale->size == affine->zero_point->size);
   for (int i = 0; i < affine->zero_point->size; i++) {
      if ((quantized_max - affine->zero_point->data[i]) * affine->scale->data[i] > 6.0f)
         return false;
   }
   return true;
}

static bool
fused_activation_supported(TfLiteFusedActivation activation, TfLiteTensor *tensor)
{
   switch (activation) {
      case kTfLiteActNone:
      case kTfLiteActRelu:
         return true;
      case kTfLiteActRelu6:
         return fused_relu6_supported(tensor);
      default:
         return false;
   }
}

static TfLiteStatus
PrepareDelegate(TfLiteContext *context, TfLiteDelegate *delegate)
{
   TfLiteIntArray *plan;
   TfLiteNode *node;
   TF_LITE_ENSURE_STATUS(context->GetExecutionPlan(context, &plan));

   // Get a list of supported nodes.
   TfLiteIntArray *supported_nodes = malloc(plan->size * sizeof(int) + sizeof(*supported_nodes));
   supported_nodes->size = plan->size;
   unsigned node_count = 0;
   for (int i = 0; i < plan->size; i++) {
      int node_index = plan->data[i];
      bool supported = false;
      TfLiteRegistration *registration;
      TF_LITE_ENSURE_STATUS(context->GetNodeAndRegistration(
          context, node_index, &node, &registration));

      switch(registration->builtin_code) {
         case kTfLiteBuiltinConv2d: {
            TfLiteTensor *input_tensor = &context->tensors[node->inputs->data[0]];
            TfLiteTensor *weight_tensor = &context->tensors[node->inputs->data[1]];
            TfLiteTensor *bias_tensor = &context->tensors[node->inputs->data[2]];
            TfLiteTensor *output_tensor = &context->tensors[node->outputs->data[0]];
            TfLiteConvParams* params = (TfLiteConvParams*)node->builtin_data;

            // Dilation and per-axis quantization not yet implemented
            if (tensor_quantization_supported(input_tensor) &&
                tensor_quantization_supported(weight_tensor) &&
                tensor_quantization_supported(bias_tensor) &&
                tensor_quantization_supported(output_tensor) &&
                fused_activation_supported(params->activation, output_tensor) &&
                (registration->version < 2 ||
                 (params->dilation_width_factor == 1 &&
                  params->dilation_height_factor == 1))) {
               supported = true;
            }
            break;
         }
         case kTfLiteBuiltinDepthwiseConv2d: {
            TfLiteTensor *input_tensor = &context->tensors[node->inputs->data[0]];
            TfLiteTensor *weight_tensor = &context->tensors[node->inputs->data[1]];
            TfLiteTensor *bias_tensor = &context->tensors[node->inputs->data[2]];
            TfLiteTensor *output_tensor = &context->tensors[node->outputs->data[0]];
            TfLiteDepthwiseConvParams* params = (TfLiteDepthwiseConvParams*)node->builtin_data;

            // Dilation and per-axis quantization not yet implemented
            if (tensor_quantization_supported(input_tensor) &&
                tensor_quantization_supported(weight_tensor) &&
                tensor_quantization_supported(bias_tensor) &&
                tensor_quantization_supported(output_tensor) &&
                fused_activation_supported(params->activation, output_tensor) &&
                (registration->version < 2 ||
                 (params->dilation_width_factor == 1 &&
                  params->dilation_height_factor == 1))) {
               supported = true;
            }
            break;
         }
         case kTfLiteBuiltinAdd: {
            supported = context->tensors[node->inputs->data[0]].data.data == NULL &&
                        context->tensors[node->inputs->data[1]].data.data == NULL;
            break;
         }
         case kTfLiteBuiltinConcatenation: {
            TfLiteConcatenationParams *params = node->builtin_data;
            supported = true;

            if (params->axis != 3 &&
                params->axis != -1)
               supported = false;

            unsigned input_channels = context->tensors[node->inputs->data[0]].dims->data[3];
            for (unsigned i = 1; i < node->inputs->size; i++)
               if (input_channels != context->tensors[node->inputs->data[i]].dims->data[3])
                  supported = false;

            break;
         }
         case kTfLiteBuiltinSplit: {
            int32_t axis = context->tensors[node->inputs->data[0]].data.i32[0];
            supported = true;

            if (axis != 3 &&
                axis != -1)
               supported = false;

            unsigned output_channels = context->tensors[node->outputs->data[0]].dims->data[3];
            for (unsigned i = 1; i < node->outputs->size; i++)
               if (output_channels != context->tensors[node->outputs->data[i]].dims->data[3])
                  supported = false;

            break;
         }
         case kTfLiteBuiltinPad: {
            uint32_t *padding = context->tensors[node->inputs->data[1]].data.data;
            supported = padding[0] == 0 &&
                        padding[1] == 0 &&
                        padding[2] == 1 &&
                        padding[3] == 1 &&
                        padding[4] == 1 &&
                        padding[5] == 1 &&
                        padding[6] == 0 &&
                        padding[7] == 0;
            break;
         }
         case kTfLiteBuiltinFullyConnected:
            supported = true;
            break;
      }

      if (supported)
         supported_nodes->data[node_count++] = node_index;
   }
   supported_nodes->size = node_count;

   TfLiteRegistration registration;

   registration.init = partition_init;
   registration.free = partition_free;
   registration.prepare = partition_prepare;
   registration.invoke = partition_invoke;

   registration.profiling_string = NULL;
   registration.builtin_code = kTfLiteBuiltinDelegate;
   registration.version = 1;
   registration.registration_external = NULL;
   registration.custom_name = "Teflon Delegate";

   // Replace supported subgraphs.
   TfLiteStatus status = context->ReplaceNodeSubsetsWithDelegateKernels(
       context,
       registration,
       supported_nodes,
       delegate);

   free(supported_nodes);

   return status;
}

static TfLiteStatus
CopyFromBufferHandle(TfLiteContext *context,
                                  TfLiteDelegate *delegate,
                                  TfLiteBufferHandle buffer_handle,
                                  TfLiteTensor *tensor)
{
   return kTfLiteOk;
}

static void
FreeBufferHandle(TfLiteContext *context,
                      TfLiteDelegate *delegate,
                      TfLiteBufferHandle *handle)
{
}

TfLiteDelegate *tflite_plugin_create_delegate(char **options_keys,
                                                char **options_values,
                                                size_t num_options,
                                                void (*report_error)(const char *));

void tflite_plugin_destroy_delegate(TfLiteDelegate *delegate);

__attribute__((visibility("default"))) TfLiteDelegate *tflite_plugin_create_delegate(char **options_keys,
                                                                                       char **options_values,
                                                                                       size_t num_options,
                                                                                       void (*report_error)(const char *))
{
   struct teflon_delegate *delegate = (struct teflon_delegate *)calloc(1, sizeof(*delegate));
   struct pipe_screen *screen;
   struct pipe_loader_device **devs;

   delegate->base.flags = kTfLiteDelegateFlagsAllowDynamicTensors | kTfLiteDelegateFlagsRequirePropagatedShapes;
   delegate->base.Prepare = &PrepareDelegate;
   delegate->base.CopyFromBufferHandle = &CopyFromBufferHandle;
   delegate->base.FreeBufferHandle = &FreeBufferHandle;

   int n = pipe_loader_probe(NULL, 0, false);
   devs = (struct pipe_loader_device **)malloc(sizeof(*devs) * n);
   pipe_loader_probe(devs, n, false);

   for (int i = 0; i < n; i++) {
      if (strstr("etnaviv", devs[i]->driver_name))
         delegate->dev = devs[i];
      else
         pipe_loader_release(&devs[i], 1);
   }
   free(devs);

   if (delegate->dev == NULL) {
      fprintf(stderr, "Couldn't open kernel device\n");
      return NULL;
   }

   teflon_debug("Teflon delegate: loaded %s driver\n", delegate->dev->driver_name);

   screen = pipe_loader_create_screen(delegate->dev, false);
   delegate->context = screen->context_create(screen, NULL, PIPE_CONTEXT_COMPUTE_ONLY);

   return &delegate->base;
}

__attribute__((visibility("default"))) void tflite_plugin_destroy_delegate(TfLiteDelegate *tflite_delegate)
{
   struct teflon_delegate *delegate = (struct teflon_delegate *)tflite_delegate;
   struct pipe_screen *screen;

   if (tflite_delegate == NULL) {
      fprintf(stderr, "tflite_plugin_destroy_delegate: NULL delegate!\n");
      return;
   }

   screen = delegate->context->screen;
   delegate->context->destroy(delegate->context);
   screen->destroy(screen);
   pipe_loader_release(&delegate->dev, 1);
   free(delegate);
}
