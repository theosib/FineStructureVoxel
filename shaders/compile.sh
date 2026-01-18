#!/bin/bash
# Compile GLSL shaders to SPIR-V
# Requires glslc (from Vulkan SDK or shaderc)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "Compiling shaders..."

# Check if glslc is available
if ! command -v glslc &> /dev/null; then
    echo "Error: glslc not found. Please install Vulkan SDK or shaderc."
    exit 1
fi

# Compile vertex shader
echo "  chunk.vert -> chunk.vert.spv"
glslc chunk.vert -o chunk.vert.spv
if [ $? -ne 0 ]; then
    echo "Failed to compile chunk.vert"
    exit 1
fi

# Compile fragment shader
echo "  chunk.frag -> chunk.frag.spv"
glslc chunk.frag -o chunk.frag.spv
if [ $? -ne 0 ]; then
    echo "Failed to compile chunk.frag"
    exit 1
fi

echo "Shader compilation complete."
