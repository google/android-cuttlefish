#!/bin/bash
# Copyright 2023 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# This script loops over all dot file in scanning_dir folder and
# generates all mapped png and svg files with same file names
# To use this script run ==>  ./graphviz_generator.sh input_fodler_path

scanning_dir=$1

# Find all visgraph dot files recuresively in scanning_dir
all_dot_file_paths=`find "$scanning_dir" -type f -name "*.dot"`

for file_path in $all_dot_file_paths
do
   echo $file_path
   # Extract file name from file path and remove extensions
   file_name="$(basename "$file_path" | cut -d. -f1)"
   # Extract file directory from file path
   file_dir="$(dirname -- $file_path)"
   # Generate png and svg output files from input dot file
   dot -Tpng $file_path > $file_dir/$file_name.png;
   dot -Tsvg $file_path > $file_dir/$file_name.svg;
done
