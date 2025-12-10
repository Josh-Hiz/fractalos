#!/bin/bash

DIRECTORY="build"

if [ -d "$DIRECTORY" ]; then
    rm -r "$DIRECTORY/*"
else
    mkdir build
fi

cd "$DIRECTORY"
cmake ..
make
cd ..