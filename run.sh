#!/bin/bash

opt -load-pass-plugin ./build/libRestrictor.so -passes="restrictor" -disable-output $1