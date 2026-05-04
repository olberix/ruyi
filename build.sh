#!/bin/bash

# rm -rf release
# cmake -B release -DCMAKE_BUILD_TYPE=Release
# cmake --build release -j$(nproc)

rm -rf debug
cmake -B debug -DCMAKE_BUILD_TYPE=Debug
cmake --build debug -j$(nproc)
