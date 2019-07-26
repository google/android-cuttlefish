#!/vendor/bin/sh

# Copyright (C) 2019 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

KERNEL_VERSION_NUMBER=`uname -r`
MAINLINE_STR='mainline'
if [[ $KERNEL_VERSION_NUMBER == *$MAINLINE_STR* ]]; then
    IS_MAINLINE=1
else
    IS_MAINLINE=0
fi

KERNEL_VERSION_NUMBER=`echo $KERNEL_VERSION_NUMBER | grep -o -E '^[0-9]+\.[0-9]+'`
# This folder on cuttlefish contains modules for multiple kernel versions.
# Hence the need to filter them instead of relying on module.order
VENDOR_MODULES='/vendor/lib/modules/*.ko'

for f in $VENDOR_MODULES
do
    MOD_VERSION=`modinfo $f`
    MOD_VERSION=`echo $MOD_VERSION | grep -o -E 'vermagic: [0-9a-zA-Z\.-]+'`
    MOD_VERSION_NUMBER=`echo $MOD_VERSION | grep -o -E '[0-9]+\.[0-9]+'`
    if [[ $MOD_VERSION == *$MAINLINE_STR* ]]; then
        IS_MOD_MAINLINE=1
    else
        IS_MOD_MAINLINE=0
    fi

    # TODO (137683279) When we have a few more kernel modules, we'll have to do the module
    # insertion of least dependencies.
    if [ $IS_MOD_MAINLINE -eq $IS_MAINLINE ] && [ $MOD_VERSION_NUMBER == $KERNEL_VERSION_NUMBER ]
    then
        `insmod $f`
        echo "Insmod " $f
    fi
done
