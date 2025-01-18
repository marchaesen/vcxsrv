#include "vpe10_config_writer.h"

void vpe10_config_writer_init(struct config_writer *writer)
{
    writer->gpu_addr_alignment = 0x2;
}
