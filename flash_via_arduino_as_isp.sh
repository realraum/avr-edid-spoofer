#!/bin/sh
avrdude -V -F -pattiny85 -P /dev/ttyACM0 -c stk500v1 -b 19200 -U flash:w:_BUILD/LCDreIDer.hex:i
