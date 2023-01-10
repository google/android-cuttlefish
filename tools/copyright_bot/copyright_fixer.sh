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

# This script loops over all dot file in parent_dir folder and all subfolders ,
# then it fixes the copyright headers based on provided file_extension and copyright_template_file
parent_dir=$1
file_extension=$2
copyright_template_file=$3

default_year_pattern="YYYY"

#find all files that doesn't contain copyright word with specific  extension
all_file_names=`grep -riL "copyright" $parent_dir --include \*.$file_extension `

#loop over list of file names
for file_name in $all_file_names
do
   #extract file creation date
   creation_date=`git log --follow --format=%as --date default $file_name | tail -1`
   # extract file creation year fron the date
   year=`echo $creation_date | awk -F\- '{print $1}'`
   echo $file_name   $year

   #read input template file
   cat $copyright_template_file >> copyright_temp_file;
   #replace the "YYYY" from template with proper extracted year
   sed -i -e "s/$default_year_pattern/$year/g" copyright_temp_file


   #echo $copyright_temp_file
   #append modified copyright header to file with no copyright
   cat $file_name >> copyright_temp_file;
   cp copyright_temp_file $file_name;
   rm copyright_temp_file;
done