#!/bin/sh
DUMPRTP=/home/dave/DVB/cvs/dvbstream/dumprtp
TS2PS=/home/dave/src/mpegtools/ts2ps
BFR=bfr
MPLAYER='mplayer -ao sdl -vo sdl -'

$DUMPRTP | $TS2PS 0 0 | $BFR -m 1024kB  | $MPLAYER
