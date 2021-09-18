/*
 * Copyright © 2020 Collabora, Ltd.
 * Author: Antonio Caggiano <antonio.caggiano@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pps.h"

#include <cerrno>
#include <cstring>

namespace pps
{
bool check(int res, const char *msg)
{
   if (res < 0) {
      char *err_msg = std::strerror(errno);
      PERFETTO_ELOG("%s: %s", msg, err_msg);
      return false;
   }

   return true;
}

} // namespace pps
