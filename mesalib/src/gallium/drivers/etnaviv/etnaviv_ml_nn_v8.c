/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * Copyright (c) 2024 Pengutronix, Philipp Zabel
 * SPDX-License-Identifier: MIT
 */

#include <time.h>
#include "util/u_inlines.h"
#include "util/u_math.h"
#include "etnaviv_context.h"
#include "etnaviv_debug.h"
#include "etnaviv_ml.h"
#include "etnaviv_ml_nn.h"
#include "etnaviv_screen.h"

static void *
map_resource(struct pipe_resource *resource)
{
   return etna_bo_map(etna_resource(resource)->bo);
}

#define FIELD(field, bits) uint32_t field : bits;

struct etna_nn_header_v8 {
   FIELD(precode, 1)
   FIELD(bit16, 1)
   FIELD(fp16, 1)
   FIELD(reserved1, 1)
   FIELD(version, 4)

   uint8_t run_length_size;
   uint8_t run_length_table[18];
   uint32_t symbol_map;
   uint16_t avg_bias;
   uint16_t reserved2;
   uint32_t stream_size[0];
};

#define MAX_TILE_WIDTH 64

static unsigned
calc_superblocks(struct etna_context *ctx, const struct etna_operation *operation, unsigned tile_x, unsigned tile_y, unsigned interleave_mode)
{
   unsigned nn_core_count = etna_ml_get_core_info(ctx)->nn_core_count;
   struct etna_core_info *info = etna_gpu_get_core_info(ctx->screen->npu);
   unsigned nn_accum_buffer_depth = info->npu.nn_accum_buffer_depth;
   unsigned output_channels = operation->output_channels;
   unsigned kernels_per_core = DIV_ROUND_UP(output_channels, nn_core_count);
   unsigned tiles_per_core;

   if (operation->weight_width == 1)
      tiles_per_core = nn_accum_buffer_depth / DIV_ROUND_UP(tile_y, interleave_mode);
   else {
      unsigned tile_size = DIV_ROUND_UP(DIV_ROUND_UP(tile_y * tile_x, operation->stride), 64);
      tiles_per_core = nn_accum_buffer_depth / (tile_size * operation->stride);
   }

   tiles_per_core = MIN2(tiles_per_core, (nn_accum_buffer_depth * 6) / 9);

   tiles_per_core = MIN2(tiles_per_core, kernels_per_core);
   tiles_per_core = MIN2(tiles_per_core, 127);

   kernels_per_core = DIV_ROUND_UP(output_channels, nn_core_count * tiles_per_core);
   unsigned num_kernels = DIV_ROUND_UP(output_channels, kernels_per_core * nn_core_count);

   return DIV_ROUND_UP(DIV_ROUND_UP(output_channels, nn_core_count), num_kernels);
}

static unsigned
calc_interleave_mode(struct etna_context *ctx, unsigned tile_width, unsigned weight_height)
{
   unsigned mode;

   if (weight_height - 1 + tile_width > (MAX_TILE_WIDTH + 8) / 2)
      return 1;

   if (tile_width <= MAX_TILE_WIDTH / 2) {
      if (MAX_TILE_WIDTH / 4 < tile_width)
         mode = 2;
      else
         mode = 4;
   } else
      mode = 1;

   if (weight_height - 1 + tile_width > (MAX_TILE_WIDTH + 8) / 4) {
      if (mode >= 2) {
         return 2;
      }
   } else {
      if (mode >= 4) {
         return 4;
      }
   }

   if (tile_width <= MAX_TILE_WIDTH / 2) {
      if (MAX_TILE_WIDTH / 4 < tile_width)
         return 2;
      else
         return 4;
   }

   return 1;
}

unsigned
etna_ml_calculate_tiling_v8(struct etna_context *ctx, const struct etna_operation *operation, unsigned *tile_width_out, unsigned *tile_height_out)
{
   unsigned nn_input_buffer_depth = etna_ml_get_core_info(ctx)->nn_input_buffer_depth;
   unsigned nn_accum_buffer_depth = etna_ml_get_core_info(ctx)->nn_accum_buffer_depth;
   unsigned input_width = operation->input_width;
   unsigned input_height = operation->input_height;
   unsigned input_channels = operation->input_channels;
   unsigned output_width = operation->output_width;
   unsigned output_height = operation->output_height;
   unsigned output_channels = operation->output_channels;
   unsigned tile_width;
   unsigned tile_height;
   unsigned superblocks;
   unsigned interleave_mode;

   if (operation->addition)
      etna_ml_calc_addition_sizes(&input_width, &input_height, &input_channels,
                                 &output_width, &output_height, &output_channels);

   if (operation->pooling_first_pixel) {
      output_width *= 2;
      output_height *= 2;
   }

   tile_width = MIN2(output_width, 64);
   interleave_mode = calc_interleave_mode(ctx, tile_width, operation->weight_height);

   tile_height = nn_input_buffer_depth * interleave_mode - operation->weight_height + 1;
   tile_height = MIN2(tile_height, interleave_mode * nn_accum_buffer_depth);
   tile_height = MIN2(tile_height, output_height);

   /* This gets us the best performance on MobileDet */
   /* TODO: Find the optimal value, or at least let the user override it */
   tile_height = MIN2(tile_height, 4);

   if (operation->stride > 1 && tile_height % 2 > 0)
      tile_height -= 1;

   tile_height = MAX2(tile_height, 1);

   superblocks = calc_superblocks(ctx, operation, tile_width, tile_height, interleave_mode);

   if (tile_width_out)
      *tile_width_out = tile_width;

   if (tile_height_out)
      *tile_height_out = tile_height;

   return superblocks;
}

static void
reorder_for_hw_depthwise(struct etna_ml_subgraph *subgraph, struct etna_operation *operation)
{
   struct pipe_context *context = subgraph->base.context;
   uint8_t *input = map_resource(operation->weight_tensor);
   struct pipe_resource *output_res = etna_ml_create_resource(context, pipe_buffer_size(operation->weight_tensor));
   uint8_t (*output)[operation->weight_width * operation->weight_height] = (void *)map_resource(output_res);

   for (int i = 0; i < operation->weight_height * operation->weight_width * operation->output_channels; i++) {
      unsigned out_channel = i % operation->output_channels;

      output[out_channel][i / operation->output_channels] = input[i];
   }

   pipe_resource_reference(&operation->weight_tensor, NULL);
   operation->weight_tensor = output_res;
}

struct bitstream {
   unsigned bits_in_buffer;
   uint64_t buffer;
   uint32_t **map;
   bool do_write;
};

static uint32_t calculate_bias_correction(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, uint8_t *weights)
{
   unsigned input_channels;
   int32_t input_zero_point = 128 - operation->input_zero_point;
   int32_t correction = 0;

   if (operation->depthwise)
      input_channels = 1;
   else if (operation->addition)
      input_channels = 2 * operation->output_channels;
   else
      input_channels = operation->input_channels;

   if (operation->weight_signed) {
      /* See etna_tensor_zero_point() */
      int8_t weight_zero_point = operation->weight_zero_point - 128;

      for (unsigned i = 0; i < operation->weight_width * operation->weight_height * input_channels; i++) {
         correction += (((int8_t *)weights)[i] - weight_zero_point) * input_zero_point;
      }
   } else {
      for (unsigned i = 0; i < operation->weight_width * operation->weight_height * input_channels; i++) {
         correction += (weights[i] - operation->weight_zero_point) * input_zero_point;
      }
   }

   return correction;
}

static void
append_bits(uint32_t value, size_t size, struct bitstream *bitstream)
{
   assert(value < 1 << size);
   if (!size)
      return;
   bitstream->buffer |= (uint64_t)value << bitstream->bits_in_buffer;
   bitstream->bits_in_buffer += size;
   if (bitstream->bits_in_buffer >= 32) {
      if (bitstream->do_write)
         **bitstream->map = bitstream->buffer & 0xffffffff;
      *bitstream->map += 1;
      bitstream->buffer >>= 32;
      bitstream->bits_in_buffer -= 32;
   }
}

static void
flush_bits(struct bitstream *bitstream)
{
   if (bitstream->bits_in_buffer > 0)
      append_bits(0, 32 - bitstream->bits_in_buffer, bitstream);
}

struct wb_stream {
   struct bitstream bitstream;
   unsigned zero_point;
   unsigned zrl_bits;
   unsigned accum_zeroes;
};

static void
wb_stream_flush_zeroes(struct wb_stream *wb_stream)
{
   struct bitstream *bitstream = &wb_stream->bitstream;

   if (wb_stream->accum_zeroes == 0)
      return;

   append_bits(wb_stream->accum_zeroes - 1, wb_stream->zrl_bits, bitstream);
   wb_stream->accum_zeroes = 0;
   append_bits(wb_stream->zero_point, 8, bitstream);
}

static void
wb_stream_write(struct wb_stream *wb_stream, unsigned value)
{
   struct bitstream *bitstream = &wb_stream->bitstream;
   unsigned max_zeroes = (1 << wb_stream->zrl_bits) - 1;

   if (wb_stream->zrl_bits == 0) {
      append_bits(value, 8, bitstream);
      return;
   }

   if (wb_stream->accum_zeroes == max_zeroes) {
      append_bits(max_zeroes, wb_stream->zrl_bits, bitstream);
      wb_stream->accum_zeroes = 0;
      append_bits(value, 8, bitstream);
      return;
   }

   if (value == wb_stream->zero_point) {
      wb_stream->accum_zeroes++;
      return;
   }

   append_bits(wb_stream->accum_zeroes, wb_stream->zrl_bits, bitstream);
   wb_stream->accum_zeroes = 0;
   append_bits(value, 8, bitstream);
}

/*
 * The V8 architecture Huffman stream decoder uses a fixed code book with 8
 * entries to determine bit lengths of variable length values later in the bit
 * stream. The 2 to 5-bit long codes are stored in fixed length 3-bit (plus
 * optional 2-bit) fields:
 *
 *     code   symbol
 *    --------------
 *    00_       0
 *    10_       1
 *    111       2
 *    110       3
 *    011       4
 *    010 1_    5
 *    010 01    6
 *    010 00    7
 *
 * The free bit (_) is used for the sign, if available, otherwise the sign
 * is stored with the variable length value later in the bitstream. In ZRL
 * encoding mode, where larger values are stored verbatim, this may also be
 * the lsb of the value instead.. The decoder processes weights in pairs and
 * is pipelined 3-deep:
 *
 * In each step, first two 3-bit codes are read, then up to two 2-bit codes
 * that belong with (010) 3-bit codes from the previous step. The optional
 * 2-bit codes from the previous step, together with the 3-bit codes from the
 * step before that are used to decode two symbols that are mapped to two bit
 * lengths for the two variable length values that are read next.
 *
 * Finally, the bit lengths, signs, and variable length values are used to
 * calculate two weights.
 */

struct code {
   /* fixed 3-bit code */
   uint8_t part0;
   /* optional 2-bit code, iff part0 == 0b010 */
   uint8_t part1;
   /* variable length value */
   uint8_t part2;
   /* bit length determined from part0, part1, and symbol-to-bitlength map */
   uint8_t part2_len;
};

struct encoder {
   /* bit-length-to-huffman-symbol map */
   uint8_t map[9];
   /* ring buffer for 3 encoded weight pairs */
   struct code code[6];
   size_t bytes_read;
   struct bitstream bitstream;
   uint32_t *initial_ptr;
   uint32_t *dest;
   uint8_t accum_zeroes;
   uint8_t avg_bias;
   bool zrl;
};

/* Calculate a histogram of bit lenghts. */
static void histogram_accumulate(size_t histogram[9], uint8_t *bytes, size_t len, bool zrl)
{
   for (size_t i = 0; i < len; i++) {
      uint8_t num_bits = 0;
      if (bytes[i]) {
         bool sign = bytes[i] >> 7;
         uint8_t value = bytes[i];
         if (sign) {
            value -= zrl;
            value ^= 0xff;
         }
         num_bits = util_logbase2(value) + 1;
      }
      assert(num_bits <= 8);
      histogram[num_bits]++;
   }
}

/*
 * value can be 8-bit raw value or variable length value with prepended sign.
 * num_bits is number of bits in value, including the sign bit.
 */
static struct code huffman_code(uint8_t sym, uint8_t value, uint8_t num_bits)
{
   switch (sym) {
   case 0:
      return (struct code){ 0 | ((value & 1) << 2), 0, value >> 1, num_bits - 1 };
   case 1:
      return (struct code){ 1 | ((value & 1) << 2), 0, value >> 1, num_bits - 1 };
   case 2:
      return (struct code){ 7, 0, value, num_bits};
   case 3:
      return (struct code){ 3, 0, value, num_bits};
   case 4:
      return (struct code){ 6, 0, value, num_bits};
   case 5:
      return (struct code){ 2, 1 | ((value & 1) << 1), value >> 1, num_bits - 1 };
   case 6:
      return (struct code){ 2, 2, value, num_bits};
   case 7:
      return (struct code){ 2, 0, value, num_bits};
   default:
      return (struct code){};
   }
}

static void emit_pair(struct encoder *encoder)
{
   struct bitstream *bitstream = &encoder->bitstream;
   struct code *code = &encoder->code[(encoder->bytes_read - 2) % 6];

   append_bits(code[0].part0, 3, bitstream);
   append_bits(code[1].part0, 3, bitstream);
   if (encoder->bytes_read > 2) {
      code = &encoder->code[(encoder->bytes_read - 4) % 6];
      append_bits(code[0].part1, code[0].part0 == 2 ? 2 : 0, bitstream);
      append_bits(code[1].part1, code[1].part0 == 2 ? 2 : 0, bitstream);
   }
   if (encoder->bytes_read > 4) {
      code = &encoder->code[(encoder->bytes_read - 6) % 6];
      append_bits(code[0].part2, code[0].part2_len, bitstream);
      append_bits(code[1].part2, code[1].part2_len, bitstream);
   }
}

/* Encode a single byte. Emit into the bitstream when a pair is complete. */
static void encode_byte(struct encoder *encoder, uint8_t byte)
{
   bool zrl = encoder->zrl;
   bool sign = byte >> 7;
   uint8_t value = byte;

   if (sign) {
      value -= zrl;
      value ^= 0xff;
   }

   uint8_t msb = util_logbase2(value);
   uint8_t num_bits = value ? (msb + 1) : 0;
   value &= ~(1 << msb);
   uint8_t sym = encoder->map[num_bits];
   if (zrl && byte == 0) {
      if (encoder->accum_zeroes <= 1) {
         // this seems to be used for the non-repeated 0 at the beginning and end
         sym = encoder->map[7];
         num_bits = 8;
      } else {
         // FIXME - how to encode run length into the run length table?
         num_bits = 1;
      }
   }
   if (!zrl && num_bits == 0) {
      num_bits = 1;
   }
   if (sym == 255 || (zrl && byte == 128)) {
      // if there is no huffman code assigned to this bit length, or when
      // encoding 0x80 in ZRL mode, dump the value into the bitstream verbatim.
      sym = encoder->map[7];
      value = byte;
      num_bits = 8;
   } else if (zrl && num_bits == 7) {
      value = byte;
      num_bits = 8;
   } else {
      value = (value << 1) | sign;
   }
   unsigned int i = encoder->bytes_read % 6;
   encoder->code[i] = huffman_code(sym, value, num_bits);
   encoder->bytes_read++;
   if ((encoder->bytes_read & 1) == 0)
      emit_pair(encoder);
}

static void
encode_value(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, struct encoder *encoder, uint8_t value)
{
   struct pipe_context *context = subgraph->base.context;
   struct etna_context *ctx = etna_context(context);
   unsigned customer_id = ctx->screen->info->customer_id;
   uint8_t zero_point = operation->weight_zero_point;

   value -= encoder->avg_bias;

   if (customer_id == 0x99) {
      if (encoder->zrl) {
         if (encoder->avg_bias > 0) {
            if (value == zero_point) {
               encoder->accum_zeroes++;
               return;
            } else if (encoder->accum_zeroes) {
               encode_byte(encoder, zero_point);
               encoder->accum_zeroes = 0;
            }
         } else {
            if (value == 0x0) {
               encoder->accum_zeroes++;
               return;
            } else if (encoder->accum_zeroes) {
               encode_byte(encoder, 0x80);
               encoder->accum_zeroes = 0;
            }
         }
      }

      encode_byte(encoder, value);
   } else {
      if (encoder->zrl) {
         if (value == zero_point) {
            encoder->accum_zeroes++;
            return;
         } else if (encoder->accum_zeroes) {
            encode_byte(encoder, 0x00);
            encoder->accum_zeroes = 0;
         }
      }

      encode_byte(encoder, value - zero_point);
   }
}

static void encoder_init(struct encoder *encoder, uint8_t *map, uint32_t *initial_ptr)
{
   memset(encoder, 0, sizeof(*encoder));
   encoder->initial_ptr = initial_ptr;
   encoder->dest = initial_ptr;
   encoder->bitstream.map = &encoder->dest;
   encoder->bitstream.do_write = initial_ptr != NULL;

   for (int i = 0; i < 9; i++)
      encoder->map[i] = 255;

   for (int i = 0; i < 8; i++) {
      assert(map[i] < sizeof(encoder->map));
      encoder->map[map[i]] = i;
   }
}

static void encode_uint32(struct encoder *encoder, uint32_t value)
{
   encode_byte(encoder, (value & 0xff) - encoder->avg_bias);
   encode_byte(encoder, ((value >> 8) & 0xff) - encoder->avg_bias);
   encode_byte(encoder, ((value >> 16) & 0xff) - encoder->avg_bias);
   encode_byte(encoder, ((value >> 24) & 0xff) - encoder->avg_bias);
}

static void encode_uint16(struct encoder *encoder, uint32_t value)
{
   encode_byte(encoder, (value & 0xff) - encoder->avg_bias);
   encode_byte(encoder, ((value >> 8) & 0xff) - encoder->avg_bias);
}

/*
 * Flush remaining weights stuck in the encoder ring buffer and all bits
 * in the bitstream FIFO. Return the total number of bits written.
 */
static size_t encoder_flush(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, struct encoder *encoder)
{
   struct bitstream *bitstream = &encoder->bitstream;
   size_t total_bits;
   uint8_t flush_val = (encoder->bytes_read & 1) + 4;

   struct code code;
   if (encoder->bytes_read & 1)
      encode_byte(encoder, 0x0);

   code.part0 = (flush_val & 1) << 2;
   code.part1 = 0x0;
   code.part2 = 0x0;
   code.part2_len = 0x0;
   encoder->code[encoder->bytes_read++ % 6] = code;
   encoder->code[encoder->bytes_read++ % 6] = code;
   emit_pair(encoder);
   encoder->code[encoder->bytes_read++ % 6] = code;
   encoder->code[encoder->bytes_read++ % 6] = code;
   emit_pair(encoder);

   total_bits = (*bitstream->map - encoder->initial_ptr) * 32 +
                bitstream->bits_in_buffer;

   int padding_bits = 0;
   if (total_bits % (64 * 8) > 0)
      padding_bits = (64 * 8) - total_bits % (64 * 8);

   while (padding_bits > 0) {
      unsigned bits = padding_bits >= 32 ? 32 : padding_bits;
      append_bits(0, bits, bitstream);
      padding_bits -= bits;
   }

   return total_bits;
}

static void map_swap(uint8_t *map, int a, int b)
{
   uint8_t tmp = map[a];

   map[a] = map[b];
   map[b] = tmp;
}

/*
 * Sort the Huffman symbol to bit length map according to the histogram of bit
 * lengths, so that more common bit lengths are represented by shorter codes.
 * FIXME - doesn't take into account zrl mode properly.
 */
static void sort_map(uint8_t *map, size_t *histogram)
{
   const uint8_t network[19][2] = {
      {0, 2}, {1, 3}, {4, 6}, {5, 7},
      {0, 4}, {1, 5}, {2, 6}, {3, 7},
      {0, 1}, {2, 3}, {4, 5}, {6, 7},
      {2, 4}, {3, 5},
      {1, 4}, {3, 6},
      {1, 2}, {3 ,4}, {5, 6},
   };

   for (int i = 0; i < 19; i++) {
      int a = network[i][0];
      int b = network[i][1];

      if (histogram[map[a]] < histogram[map[b]])
         map_swap(map, a, b);
   }
}

static void encoder_reset(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, struct encoder *encoder)
{
   encoder->initial_ptr = *encoder->bitstream.map;
   encoder->dest = encoder->initial_ptr;
   encoder->bitstream.map = &encoder->dest;

   encoder->bitstream.buffer = 0;
   encoder->bitstream.bits_in_buffer = 0;
   encoder->bytes_read = 0;
   memset(encoder->code, 0, sizeof(encoder->code));
}

static void encode_superblock(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, struct encoder *encoder, unsigned kernels_in_superblock, unsigned first_channel)
{
   struct pipe_context *pctx = subgraph->base.context;
   struct etna_context *ctx = etna_context(pctx);
   unsigned nn_core_count = etna_ml_get_core_info(ctx)->nn_core_count;
   unsigned input_channels = operation->input_channels;
   unsigned output_channels = operation->output_channels;
   unsigned kernel_size;
   uint8_t *weights = map_resource(operation->weight_tensor);
   unsigned block_size;
   unsigned blocks;

   if (operation->depthwise)
      input_channels = 1;
   else if (operation->addition)
      input_channels = 2 * output_channels;

   kernel_size = input_channels * operation->weight_height * operation->weight_width;

   uint8_t (*weights_map)[kernel_size] = (void *)weights;

   if (operation->depthwise)
      block_size = MAX2(operation->weight_height * operation->weight_width, 9);
   else
      block_size = 9;

   blocks = DIV_ROUND_UP(kernel_size, block_size);

   for (unsigned block = 0; block < blocks; block++) {
      for (unsigned kernel = 0; kernel < kernels_in_superblock; kernel++) {
         unsigned oc;

         if (operation->depthwise) {
            oc = first_channel + kernel * nn_core_count;

            if (output_channels > 1 && oc >= (output_channels - output_channels % nn_core_count))
               oc -= nn_core_count - output_channels % nn_core_count;
         } else
            oc = first_channel + kernel;

         for (unsigned kernel_idx = 0; kernel_idx < block_size; kernel_idx++) {
            uint8_t weight;

            if (kernel_idx + block * block_size >= kernel_size)
               weight = operation->weight_zero_point;
            else if (operation->weight_signed)
               weight = ((int8_t *)(weights_map[oc]))[kernel_idx + block * block_size] + 128;
            else
               weight = weights_map[oc][kernel_idx + block * block_size];

            encode_value(subgraph, operation, encoder, weight);
         }

         if (operation->depthwise && block_size % 9)
            for (unsigned i = 0; i < 9 - block_size % 9; i++)
               encode_value(subgraph, operation, encoder, operation->weight_zero_point);
      }
   }
}

static uint32_t pack_symbol_map(uint8_t map[8])
{
   uint32_t ret = 0;

   for (int i = 0; i < 8; i++)
      ret |= map[i] << (4 * i);

   return ret;
}

static struct etna_bo *
create_bo(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation)
{
   struct pipe_context *context = subgraph->base.context;
   struct etna_context *ctx = etna_context(context);
   unsigned nn_core_count = etna_ml_get_core_info(ctx)->nn_core_count;
   unsigned input_channels = operation->input_channels;
   unsigned output_channels = operation->output_channels;
   unsigned cores_used = MIN2(output_channels, nn_core_count);
   size_t max_size;

   if (operation->depthwise)
      input_channels = 1;
   else if (operation->addition)
      input_channels = 2 * output_channels;

   unsigned header_size = 64;
   unsigned body_size = ALIGN(DIV_ROUND_UP(output_channels, cores_used) * (input_channels * operation->weight_height * operation->weight_width + 4 + 4), 64) * 2;
   unsigned tail_size = 64;
   max_size = header_size + cores_used * body_size + tail_size;

   return etna_ml_create_bo(context, max_size);
}

static void
calculate_symbol_map(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, uint8_t *symbol_map)
{
   unsigned input_channels = operation->input_channels;
   unsigned output_channels = operation->output_channels;
   uint8_t *input = map_resource(operation->weight_tensor);
   size_t histogram[9] = {};

   if (operation->depthwise)
      input_channels = 1;
   else if (operation->addition)
      input_channels = 2 * output_channels;

   uint8_t (*weights_map)[input_channels][operation->weight_height][operation->weight_width] = (void *)input;
   unsigned kernel_size = operation->weight_width * operation->weight_height * input_channels;
   for (unsigned oc = 0; oc < output_channels; oc++)
      histogram_accumulate(histogram, (uint8_t *)weights_map[oc], kernel_size, false);

   for (int i = 0; i < 8; i++)
      symbol_map[i] = i;
   sort_map(symbol_map, histogram);
}

static void
fill_weights(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, struct encoder *encoder, struct etna_nn_header_v8 *header)
{
   struct pipe_context *context = subgraph->base.context;
   struct etna_context *ctx = etna_context(context);
   unsigned output_channels = operation->output_channels;
   unsigned nn_core_count = etna_ml_get_core_info(ctx)->nn_core_count;
   unsigned cores_used = MIN2(output_channels, nn_core_count);
   unsigned superblocks = etna_ml_calculate_tiling_v8(ctx, operation, NULL, NULL);
   unsigned full_superblock = DIV_ROUND_UP(output_channels, nn_core_count * superblocks);

   unsigned channel_per_superblock[superblocks];
   for (unsigned superblock = 0; superblock < superblocks; superblock++)
      channel_per_superblock[superblock] = superblock * full_superblock * cores_used;

   for (unsigned core = 0; core < cores_used; core++) {
      unsigned kernels_per_core = output_channels / cores_used;
      if (core < output_channels % cores_used)
         kernels_per_core++;

      encoder_reset(subgraph, operation, encoder);
      encode_uint16(encoder, kernels_per_core);

      for (unsigned superblock = 0; superblock < superblocks; superblock++) {

         unsigned kernels_in_superblock = full_superblock;
         if (superblock == superblocks - 1) {
            unsigned remaining_channels = output_channels - cores_used * (superblocks - 1) * full_superblock;
            kernels_in_superblock = remaining_channels / cores_used;
            if (core < remaining_channels % cores_used)
               kernels_in_superblock += 1;
         }

         unsigned first_channel;
         if (operation->depthwise)
            first_channel = cores_used - core - 1 + cores_used * full_superblock * superblock;
         else
            first_channel = channel_per_superblock[superblock];

         encode_superblock(subgraph, operation, encoder, kernels_in_superblock, first_channel);

         channel_per_superblock[superblock] += kernels_in_superblock;
      }

      unsigned actual_bits = encoder_flush(subgraph, operation, encoder);
      header->stream_size[core] = actual_bits;
   }
}

static uint32_t *
fill_biases(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, uint32_t *map)
{
   uint8_t *input = map_resource(operation->weight_tensor);
   uint32_t *biases = map_resource(operation->bias_tensor);
   unsigned input_channels = operation->input_channels;
   unsigned output_channels = operation->output_channels;

   if (operation->depthwise)
      input_channels = 1;
   else if (operation->addition)
      input_channels = 2 * output_channels;

   uint8_t (*weights_map)[input_channels][operation->weight_height][operation->weight_width] = (void *)input;
   for (unsigned oc = 0; oc < output_channels; oc++) {
      uint32_t corr = calculate_bias_correction(subgraph, operation, (uint8_t *)weights_map[oc]);

      *map = biases[oc] + corr;
      map++;
   }

   return map;
}

struct etna_bo *
etna_ml_create_coeffs_v8(struct etna_ml_subgraph *subgraph, const struct etna_operation *operation, unsigned *cache_size)
{
   struct etna_bo *bo = create_bo(subgraph, operation);
   uint32_t *map = etna_bo_map(bo);
   struct etna_nn_header_v8 *header = (struct etna_nn_header_v8 *)map;
   struct encoder encoder;
   uint8_t symbol_map[8];

   etna_bo_cpu_prep(bo, DRM_ETNA_PREP_WRITE);

   calculate_symbol_map(subgraph, operation, symbol_map);
   header->symbol_map = pack_symbol_map(symbol_map);
   header->version = 1;

   map += ALIGN(sizeof(*header), 64) / 4;

   encoder_init(&encoder, symbol_map, map);

   fill_weights(subgraph, operation, &encoder, header);
   map = fill_biases(subgraph, operation, encoder.dest);

   /* Size of the data that will go into the SRAM cache, header included */
   *cache_size = (uint8_t*)map - (uint8_t*)etna_bo_map(bo);

   etna_bo_cpu_fini(bo);

   return bo;
}
