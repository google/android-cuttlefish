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

#apply template 1 to these file extensions
scanning_dir=$1
bot_dir=$(dirname "$0")
sh $bot_dir/copyright_fixer.sh $scanning_dir "cpp" $bot_dir/copyright_template_1.txt
sh $bot_dir/copyright_fixer.sh $scanning_dir "cc" $bot_dir/copyright_template_1.txt
sh $bot_dir/copyright_fixer.sh $scanning_dir "h" $bot_dir/copyright_template_1.txt
sh $bot_dir/copyright_fixer.sh $scanning_dir "java" $bot_dir/copyright_template_1.txt
sh $bot_dir/copyright_fixer.sh $scanning_dir "proto" $bot_dir/copyright_template_1.txt
sh $bot_dir/copyright_fixer.sh $scanning_dir "js" $bot_dir/copyright_template_1.txt
sh $bot_dir/copyright_fixer.sh $scanning_dir "css" $bot_dir/copyright_template_1.txt

#apply template 2 to these file extensions
sh $bot_dir/copyright_fixer.sh $scanning_dir "rs" $bot_dir/copyright_template_2.txt
sh $bot_dir/copyright_fixer.sh $scanning_dir "go" $bot_dir/copyright_template_2.txt

#apply template 3 to these file extensions
sh $bot_dir/copyright_fixer.sh $scanning_dir "html" $bot_dir/copyright_template_3.txt

#apply template 4 to these file extensions
sh $bot_dir/copyright_fixer.sh $scanning_dir "xml" $bot_dir/copyright_template_4.txt

#apply template 5 to these file extensions
sh $bot_dir/copyright_fixer.sh $scanning_dir "sh" $bot_dir/copyright_template_5.txt
sh $bot_dir/copyright_fixer.sh $scanning_dir "bp" $bot_dir/copyright_template_5.txt
sh $bot_dir/copyright_fixer.sh $scanning_dir "mk" $bot_dir/copyright_template_5.txt