# Copyright (c) Facebook, Inc. and its affiliates.
#
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

if(${VELOX_BUILD_TESTING})
  add_subdirectory(tests)
endif()

velox_add_library(velox_common_compression Compression.cpp LzoDecompressor.cpp)
velox_link_libraries(
  velox_common_compression
  PUBLIC velox_status Folly::folly
  PRIVATE velox_exception)

if(VELOX_ENABLE_COMPRESSION_LZ4)
  velox_sources(velox_common_compression PRIVATE Lz4Compression.cpp
                HadoopCompressionFormat.cpp)
  velox_link_libraries(velox_common_compression PUBLIC lz4::lz4)
  velox_compile_definitions(velox_common_compression
                            PRIVATE VELOX_ENABLE_COMPRESSION_LZ4)
endif()
