#!/bin/sh

solve-field $1

if [ -e $1.solved ]; then
    eog $1-ngc.png
fi
