#!/usr/bin/env bash

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

cat $DIR/visualize/head.py $1 $DIR/visualize/tail.py > /tmp/visualize.py
python /tmp/visualize.py > /tmp/visualize.dot
xdot /tmp/visualize.dot
