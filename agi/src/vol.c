#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "platform_support.h"
#include "vol.h"

typedef struct {
	uint8_t vol_number;
	uint32_t offset;
} resPos_t;

resPos_t _find_res_offset(const char* dir_filename, const uint8_t res_no) {
	agi_file_t dir_file = get_file(dir_filename);

	unsigned int offset = res_no * 3;

	if (offset >= dir_file.size) {
		free_file(dir_file);
		panic("Directory file offset out of bounds for file '%s', resource no %d", dir_filename, res_no);
	}

	uint8_t a = *(dir_file.data + offset + 0);
	uint8_t b = *(dir_file.data + offset + 1);
	uint8_t c = *(dir_file.data + offset + 2);

	free_file(dir_file);

	uint32_t entry = (a << 16) | (b << 8) | c;
	return (resPos_t) { .vol_number = a >> 4, .offset = entry & 0xFFFFF };
}

// NOTE! CALLER IS RESPONSIBLE FOR FREEING BUFFER
vol_data_t load_vol_data(const char* dir_filename, const uint8_t res_no, bool is_logic) {
	resPos_t res_pos = _find_res_offset(dir_filename, res_no);

	char path[8];
	sprintf(path, "vol.%d", res_pos.vol_number);

	// Read only the 2-byte size header from the VOL file instead of loading it all.
	uint8_t size_buf[2];
	read_file_at(path, res_pos.offset + 3, size_buf, 2);

	vol_data_t result;
	result.size = size_buf[0] | (size_buf[1] << 8);

	result.buffer = malloc(result.size + (is_logic ? 1 : 0));
	if (result.buffer) {
		read_file_at(path, res_pos.offset + 5, result.buffer, result.size);
	} else {
		panic("Failed to allocate memory for resource");
	}

	return result;
}

bool discard_vol_data(vol_data_t* vol_data) {
	if (vol_data->buffer) {
		free(vol_data->buffer);
		vol_data->buffer = NULL;
		vol_data->size = 0;
		return true;
	}
	return false;
}