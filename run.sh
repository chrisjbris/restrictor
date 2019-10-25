#!/bin/bash

opt -load-pass-plugin ./build/libRestrictor.so -passes="restrictor" -S $1 -o out.ll