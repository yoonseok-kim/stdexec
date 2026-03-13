#=============================================================================
# Copyright 2024 NVIDIA Corporation
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
#=============================================================================

# Basic examples for learning sender-based asynchronous execution model
# These examples demonstrate core concepts of P2300 std::execution

set(basic_example_list
    "example.basic.01_just_and_sync_wait : 01_just_and_sync_wait.cpp"
    "example.basic.02_then : 02_then.cpp"
    "example.basic.03_let_value : 03_let_value.cpp"
    "example.basic.04_schedule : 04_schedule.cpp"
    "example.basic.05_when_all : 05_when_all.cpp"
    "example.basic.06_starts_on_continues_on : 06_starts_on_continues_on.cpp"
    "example.basic.07_split : 07_split.cpp"
)

set(basic_example_targets)

foreach(example ${basic_example_list})
    def_example(${example})
    # Extract target name from "target : source" format
    string(REGEX REPLACE " *:.*" "" target_name "${example}")
    string(STRIP "${target_name}" target_name)
    list(APPEND basic_example_targets ${target_name})
endforeach()

# Custom target to build all basic examples at once
add_custom_target(basic_examples DEPENDS ${basic_example_targets})

# Register each example as a test
foreach(target ${basic_example_targets})
    add_test(NAME ${target} COMMAND ${target})
endforeach()
