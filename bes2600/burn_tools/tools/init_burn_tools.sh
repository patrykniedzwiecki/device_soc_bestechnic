#!/bin/bash
# Copyright (c) 2021 bestechnic (Shanghai) Technologies CO., LIMITED.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

gui_path_src=$1
gui_path_dest=$2

if [ ! -d "write_flash_gui/" ];then
    wget -P ./../ https://downloads.openharmony.cn/tools/bestechnic/burn_tools_v1.0.tar.gz
    tar -zxvf ./../burn_tools_v1.0.tar.gz -C ../
fi
mkidr -p ${gui_path_dest}/auto_build_tool/hash_sig
mkdir -p ${gui_path_dest}/release_bin
cp -rf ${gui_path_src}/write_flash_gui ${gui_path_dest}
