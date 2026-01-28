#!/bin/bash

time HOME=$PWD ./bin/launch_cvd --daemon

HOME=$PWD ./bin/stop_cvd
