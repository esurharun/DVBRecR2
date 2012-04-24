#!/bin/sh
DUMPRTP=/home/dave/DVB/dvbstream-0.2/dumprtp
TS2ES=/home/dave/DVB/DVB/apps/mpegtools/ts2es
MPG123='mpg123 -'

$DUMPRTP | $TS2ES 2 | $MPG123
