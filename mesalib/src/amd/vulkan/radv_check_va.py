#!/usr/bin/python3

import re
import sys

def main():
    if len(sys.argv) != 3:
        print("Missing arguments: ./radv_check_va.py <bo_history> <64-bit VA>")
        sys.exit(1)

    bo_history = str(sys.argv[1])
    va = int(sys.argv[2], 16)

    va_found = False
    with open(bo_history) as f:
        for line in f:
            p = re.compile('timestamp=(.*), VA=(.*)-(.*), destroyed=(.*), is_virtual=(.*)')
            mapped_p = re.compile('timestamp=(.*), VA=(.*)-(.*), mapped_to=(.*)')
            m = p.match(line)
            mapped_m = mapped_p.match(line)
            if m:
                va_start = int(m.group(2), 16)
                va_end = int(m.group(3), 16)
                mapped_va = 0
            elif mapped_m:
                va_start = int(mapped_m.group(2), 16)
                va_end = int(mapped_m.group(3), 16)
                mapped_va = int(mapped_m.group(4), 16)
            else:
                continue

            # Check if the given VA was ever valid and print info.
            if va >= va_start and va < va_end:
                print("VA found: %s" % line, end='')
                if mapped_m:
                    effective_va = (va - va_start) + mapped_va
                    if mapped_va == 0:
                        effective_va = 0
                    print("  Virtual mapping: %016x -> %016x" % (va, effective_va))
                va_found = True
    if not va_found:
        print("VA not found!")

if __name__ == '__main__':
    main()
