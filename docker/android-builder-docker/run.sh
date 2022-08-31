#!/bin/bash
dst_dir=$1
prog=$2
shift 2
this_dir=$PWD
cd $dst_dir
$prog "$@"
cd $this_dir
