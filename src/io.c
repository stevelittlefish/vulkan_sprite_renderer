#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

char *read_entire_binary_file(const char *filename, size_t *size) {
	printf(" Reading file %s\n", filename);

	// Create a buffer to read the file into
	size_t buffer_size = 1024;
	char *buffer = malloc(buffer_size);
	int bytes_read = 0;
	
	// Open the file
	FILE *file = fopen(filename, "rb");

	if (!file) {
		fprintf(stderr, "Failed to open file %s\n", filename);
		exit(1);
	}

	// Read the file into the buffer one character at a time
	int c;
	while ((c = fgetc(file)) != EOF) {
		// If the buffer is full, increase its size
		if ((size_t) bytes_read >= buffer_size) {
			buffer_size += 1024;
			buffer = realloc(buffer, buffer_size);

			if (buffer == NULL) {
				fprintf(stderr, "Failed to allocate memory for file %s\n", filename);
				fclose(file);
				exit(1);
			}
		}

		buffer[bytes_read++] = c;
	}

	// Close the file
	fclose(file);
	
	// Shrink the buffer to the actual size of the file
	buffer = realloc(buffer, bytes_read);

	*size = bytes_read;
	return buffer;
}
