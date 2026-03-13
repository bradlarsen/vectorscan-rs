#
#  Copyright (c) 2020-2023, VectorCamp PC
#  
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#  
#   * Redistributions of source code must retain the above copyright notice,
#     this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#   * Neither the name of Intel Corporation nor the names of its contributors
#     may be used to endorse or promote products derived from this software
#     without specific prior written permission.
#  
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#
import json
import sys

#reads from the clang-tidy config file the first comment to ignore specific files
# Get the paths from the command-line arguments
# python3 ../source/scripts/change_command.py ../source/.clang-tidy ./compile_commands.json
clang_tidy_config_path = sys.argv[1]
compile_commands_path = sys.argv[2]

# Load the data from the file
with open(compile_commands_path, 'r') as f:
    data = json.load(f)

# Open the clang-tidy config file and read the first comment
with open(clang_tidy_config_path, 'r') as f:
    for line in f:
        if line.startswith('#'):
            ignore_files = line[1:].strip().split(',')
            break

# Filter out the entries for the ignored files
data = [entry for entry in data if not any(ignore_file in entry['file'] for ignore_file in ignore_files)]

# Write the result to the same file
with open(compile_commands_path, 'w') as f:
    json.dump(data, f, indent=2)