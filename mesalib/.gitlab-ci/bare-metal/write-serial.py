#!/usr/bin/python3

import sys
import serial

dev = sys.argv[1]
command = sys.argv[2] + '\n'

ser = serial.Serial(dev, 115200, timeout=5)
ser.write(command.encode())
ser.close()
