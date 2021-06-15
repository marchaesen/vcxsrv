void
demo_cmdbuf(uint64_t *buf, size_t size,
            struct agx_pool *pool,
            uint64_t encoder_ptr,
            unsigned width, unsigned height,
            uint32_t pipeline_null,
            uint32_t pipeline_clear,
            uint32_t pipeline_store,
            uint64_t rt0);

void
demo_mem_map(void *map, size_t size, unsigned *handles,
             unsigned count, uint64_t cmdbuf_id, uint64_t
             encoder_id);

void
agx_internal_shaders(struct agx_device *dev);
