#!/bin/bash

# Directory containing the shaders
SHADER_DIR="shaders"

# Check if the directory exists
if [ ! -d "$SHADER_DIR" ]; then
	echo "Directory $SHADER_DIR not found!"
	exit 1
fi

# Loop through all files in the shaders directory
for SHADER_FILE in "$SHADER_DIR"/*; do
	# Get the filename with extension
	FILENAME=$(basename "$SHADER_FILE")

  # Generate the output filename by appending .spv to the original filename
  OUTPUT_FILE="$SHADER_DIR/${FILENAME}.spv"

  # Check if the file is a valid shader file (you can add more extensions as needed)
  if [[ "$SHADER_FILE" == *.vert || "$SHADER_FILE" == *.frag ]]; then
	  # Compile the shader
	  echo "Compiling $SHADER_FILE to $OUTPUT_FILE..."
	  glslc "$SHADER_FILE" -o "$OUTPUT_FILE"

	# Check if the compilation was successful
	if [ $? -eq 0 ]; then
		echo "Successfully compiled $SHADER_FILE"
	else
		echo "Failed to compile $SHADER_FILE"
		exit 1
	fi
  fi
done

echo "Shader compilation complete."
