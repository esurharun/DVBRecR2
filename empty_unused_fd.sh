#!/bin/bash
find -L /proc/*/fd -type f -links 0 2>&1 | grep -v such | awk '{ system("echo > " $1) }'
