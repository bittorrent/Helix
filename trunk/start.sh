#!/bin/sh

ulimit -n 10000
./stage/helix --pidfile helix.pid -d true 6969 --logfile helix.log

