#include <string.h>
#include <ctype.h>

#include "../state.h"
#include "../actions.h"
#include "../platform_support.h"
#include "../text_display.h"
#include "../text_parser.h"
#include "../input_queue.h"

void get_string(uint8_t str, uint8_t msg, uint8_t col, uint8_t row, uint8_t maxLen) {
	// AGI bytecode order: get.string(str, msg, screen_row, screen_col, len).
	// The engine parameter names are swapped: `col` = screen_row (y/8),
	//                                          `row` = screen_col (x/8).
	uint8_t screen_row = col;
	uint8_t screen_col = row;

	const char* prompt = get_message(state.current_logic, msg);

	// Draw the prompt at the game-specified position.
	uint8_t dr = screen_row, dc = screen_col;
	_draw_text(&dr, &dc, prompt, 15, 0);
	// dr/dc now point to the character cell just after the prompt.

	char buf[40] = "";
	uint8_t pos = 0;
	if (maxLen > 39) maxLen = 39;
	state.enter_pressed = false;
	queue_pos = 0;

	while (1) {
		// Erase and redraw input area on the same row as the end of the prompt.
		for (uint8_t i = 0; i <= maxLen; i++)
			_draw_char((dc + i) * 8, dr * 8, ' ', 15, 0);
		uint8_t tr = dr, tc = dc;
		_draw_text(&tr, &tc, buf, 15, 0);
		_draw_char((dc + pos) * 8, dr * 8, '_', 15, 0);

		platform_flush_display();
		check_key();

		for (int i = 0; i < queue_pos; i++) {
			input_queue_entry_t entry = queue[i];
			if (entry.ascii >= 0x20 && entry.ascii < 0x7F && pos < maxLen) {
				buf[pos++] = entry.ascii;
				buf[pos] = '\0';
			} else if (entry.ascii == '\b' && pos > 0) {
				buf[--pos] = '\0';
			} else if (entry.ascii == 27) {
				state.strings[str][0] = '\0';
				queue_pos = 0;
				_clear_input_rows(screen_row, dr);
				return;
			}
		}
		queue_pos = 0;

		if (state.enter_pressed) {
			state.enter_pressed = false;
			strncpy(state.strings[str], buf, 40);
			state.strings[str][39] = '\0';
			_clear_input_rows(screen_row, dr);
			return;
		}
	}
}

// Clear all character rows from first_row to last_row inclusive.
void _clear_input_rows(uint8_t first_row, uint8_t last_row) {
	for (uint8_t r = first_row; r <= last_row && r < 25; r++)
		for (uint8_t c = 0; c < 40; c++)
			_draw_char(c * 8, r * 8, ' ', 0, 0);
}

void parse(uint8_t str) {
	// Lowercase the string, copy to input buffer, run the word-group parser.
	const char* src = state.strings[str];
	size_t len = strlen(src);
	if (len >= sizeof(system_state.input_buffer))
		len = sizeof(system_state.input_buffer) - 1;
	for (size_t i = 0; i < len; i++)
		system_state.input_buffer[i] = (char)tolower((unsigned char)src[i]);
	system_state.input_buffer[len] = '\0';
	system_state.input_pos = (uint8_t)len;

	system_state.num_parsed_word_groups = 0;
	parse_word_groups();

	// parse() presents the string as freshly-entered input. Clear the
	// "said already accepted this cycle" latch (FLAG_4) so a said() after this
	// parse can still match — otherwise, if an earlier said() (e.g. the command
	// that opened the search dialogue) consumed the command-line input this
	// cycle, the re-parsed string is ignored and the player must type it twice.
	state.flags[FLAG_4_SAID_ACCEPTED_INPUT] = false;

	if (system_state.num_parsed_word_groups > 0)
		state.flags[FLAG_2_COMMAND_ENTERED] = true;

	// input_buffer/input_pos were only scratch for the word-group parser — the
	// text came from a string var (get.string), not the command line. Clear them
	// so the parsed string doesn't linger on the command prompt after the dialogue.
	system_state.input_buffer[0] = '\0';
	system_state.input_pos = 0;
}

void set_string(uint8_t str, uint8_t msg) {
	const char* message = get_message(state.current_logic, msg);
	strcpy(state.strings[str], message);
}

void word_to_string(uint8_t word, uint8_t str) {
	UNIMPLEMENTED
}
