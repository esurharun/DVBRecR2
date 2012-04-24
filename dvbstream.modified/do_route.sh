#!/bin/sh
# SET UP MULTICAST ROUTING (MUST BE ROOT)
/sbin/route add -net 224.0.0.0 netmask 240.0.0.0 dev eth0
