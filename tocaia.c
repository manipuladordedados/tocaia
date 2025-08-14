/* Portable TUI Gopher client written in C89 for POSIX systems.
 *
 * Copyright 2025 Valter Nazianzeno <manipuladordedados at gmail dot com>
 *
 * See LICENSE file for copyright and license details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <ctype.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>
#include <netinet/in.h>

#define PROGRAM_VERSION "0.7.0"

#define CRLF "\r\n"

#define INITIAL_BUFFER_SIZE 4096
#define MAX_HOST_LENGTH 256
#define MAX_SELECTOR_LENGTH 1024
#define MAX_DISPLAY_LENGTH 1024
#define MAX_CONTENT_DISPLAY_WIDTH 78
#define MAX_URL_INPUT_LENGTH (MAX_HOST_LENGTH + MAX_SELECTOR_LENGTH + 10)

/* A boolean type for C89 compatibility. */
typedef int BOOL;
#define TRUE 1
#define FALSE 0

/* ANSI Color Definitions */
#define COLOR_RESET         "\033[0m"
#define TEXT_COLOR          "\033[1;33m"
#define DIRECTORY_COLOR     "\033[1;32m"
#define CSO_COLOR           "\033[1;36m"
#define ERROR_COLOR         "\033[1;31m"
#define BINARY_COLOR        "\033[1;35m"
#define SEARCH_COLOR        "\033[1;34m"
#define TELNET_COLOR        "\033[1;37m"
#define GIF_COLOR           "\033[1;35m"
#define HTML_COLOR          "\033[1;36m"
#define INFO_COLOR          "\033[0;90m"
#define UNKNOWN_COLOR       "\033[1;91m"
#define SELECTED_ITEM_COLOR "\033[1;30;47m"
#define HEADER_COLOR        "\033[1;96m"
#define FOOTER_COLOR        "\033[1;94m"
#define SEPARATOR_COLOR     "\033[0;90m"
#define HEADER_BG           "\033[48;5;17m"
#define HEADER_FG           "\033[1;37m"

/* Key Code Definitions */
#define KEY_UP              'A'
#define KEY_DOWN            'B'
#define KEY_PGUP            '5'
#define KEY_PGDN            '6'
#define KEY_ENTER           '\n'
#define KEY_CARRIAGE_RETURN '\r'
#define KEY_BACKSPACE       127
#define KEY_ESC             27

/* Represents a single item in a Gopher menu. */
typedef struct GopherItem {
	char type;
	char display_string[MAX_DISPLAY_LENGTH];
	char selector[MAX_SELECTOR_LENGTH];
	char host[MAX_HOST_LENGTH];
	int port;
	BOOL is_selectable;
	int menu_index;
} GopherItem;

/* Represents a node in the navigation history (a doubly-linked list). */
typedef struct NavigationState {
	char host[MAX_HOST_LENGTH];
	int port;
	char selector[MAX_SELECTOR_LENGTH];
	char *page_content;
	struct NavigationState *prev;
	struct NavigationState *next;
} NavigationState;

/* Holds the entire state of the application. */
typedef struct AppState {
	NavigationState *current_nav;
	GopherItem *gopher_items;
	int total_items;
	int selectable_items;
	int selected_index;
	int scroll_offset;
	int text_scroll_line;
	int total_content_lines;
	BOOL is_running;
	struct winsize terminal_size;
} AppState;

/* Flag to indicate a pending terminal resize signal. Must be volatile. */
volatile sig_atomic_t g_resize_pending = 0;
/* Stores the original terminal settings to restore on exit. */
struct termios g_original_termios;

void run_main_loop(AppState *state);
void fetch_current_content(AppState *state);
BOOL is_gopher_menu(const NavigationState *nav);
void calculate_text_lines(AppState *state, const char *content);

void trim_whitespace(char* str);
BOOL parse_gopher_line(char* line, GopherItem* item, const char* current_host, int current_port);
void process_gopher_response(AppState* state, const char *data);

void handle_menu_navigation(AppState *state, char input);
void handle_menu_action(AppState *state, char input);
BOOL handle_gopher_menu_interaction(AppState* state);
BOOL handle_text_viewer_interaction(AppState* state);
void handle_search_prompt(AppState *state, const GopherItem *item);
void handle_open_prompt(AppState *state);

void get_current_url(const NavigationState* nav, char* buffer, size_t size);
void draw_header(const AppState* state);
void draw_gopher_menu(AppState* state);
void draw_text_viewer(AppState* state, const char *content);
void show_about_screen(const AppState* state);

NavigationState* create_nav_state(const char *host, int port, const char *selector);
void free_forward_history(NavigationState *current_state);
void free_navigation_history(NavigationState *head);
void navigate_to(AppState *state, const char *host, int port, const char *selector);
void navigate_back(AppState *state);
void navigate_forward(AppState *state);

void setup_terminal_for_app(void);
void restore_terminal(void);
void handle_resize_signal(int sig);
void handle_sigint_signal(int sig);
void set_cursor_visibility(int visible);
void clear_terminal(void);
void move_cursor(int row, int col);
void print_string_at(const char *str, int row, int col);
void print_centered_string(const char *str, int row, int term_width);
void clear_line(int row, int term_width);

int connect_and_send_request(const char *host, int port, const char *selector);
char *receive_gopher_data(int sock);

void die(const char *msg);
const char* get_gopher_type_description(char type);
const char* get_gopher_item_color(char type, BOOL selected);
void show_help(void);
void show_version(void);
BOOL parse_gopher_address(const char *address, char *host_out, int *port_out, char *selector_out);

int main(int argc, char *argv[]) {
	AppState state;
	char initial_host[MAX_HOST_LENGTH];
	int initial_port;
	char initial_selector[MAX_SELECTOR_LENGTH];

	if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		show_help();
		return EXIT_SUCCESS;
	}
	if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
		show_version();
		return EXIT_SUCCESS;
	}

	/* Try to parse the Gopher address. If it fails, print an error and exit. */
	if (!parse_gopher_address(argv[1], initial_host, &initial_port, initial_selector)) {
		die("Error: Invalid Gopher address format.");
	}

	/* From this point on, the URL is valid, so the terminal will be configured. */
	/* atexit() ensures restore_terminal() is called on any normal or error exit. */
	atexit(restore_terminal);
	setup_terminal_for_app();

	/* Initialize the application state */
	memset(&state, 0, sizeof(AppState));
	state.is_running = TRUE;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &state.terminal_size);
	navigate_to(&state, initial_host, initial_port, initial_selector);

	if (!state.current_nav) {
		die("Error: Failed to initialize navigation state.");
	}

	run_main_loop(&state);

	/* Clean up all allocated resources before exiting. */
	free_navigation_history(state.current_nav);
	if (state.gopher_items) {
		free(state.gopher_items);
	}

	return EXIT_SUCCESS;
}

/* Main application loop. */
void run_main_loop(AppState *state) {
	while (state->is_running) {
		if (g_resize_pending) {
			ioctl(STDOUT_FILENO, TIOCGWINSZ, &state->terminal_size);
			g_resize_pending = 0;
		}

		if (state->current_nav == NULL) {
			state->is_running = FALSE;
			continue;
		}

		/* Fetch content if it hasn't been loaded yet for the current state. */
		if (state->current_nav->page_content == NULL) {
			fetch_current_content(state);
		}

		/* Decide whether to show a menu or a text file. */
		if (is_gopher_menu(state->current_nav)) {
			process_gopher_response(state, state->current_nav->page_content);
			if (!handle_gopher_menu_interaction(state)) {
				state->is_running = FALSE;
			}
		} else {
			calculate_text_lines(state, state->current_nav->page_content);
			if (!handle_text_viewer_interaction(state)) {
				state->is_running = FALSE;
			}
		}
	}
}

/* Fetches the Gopher content for the current navigation state. */
void fetch_current_content(AppState *state) {
	int sock;
	char *response;

	sock = connect_and_send_request(state->current_nav->host, state->current_nav->port, state->current_nav->selector);
	if (sock == -1) {
		die("Error: Failed to connect to the Gopher server.");
	}

	response = receive_gopher_data(sock);
	close(sock);

	if (response == NULL) {
		die("Error: Failed to receive data from the Gopher server.");
	}

	state->current_nav->page_content = response;
}

/* Determines if the current content should be treated as a Gopher menu. */
BOOL is_gopher_menu(const NavigationState *nav) {
	char selector_type;
	char *first_line_end;
	char first_line[MAX_DISPLAY_LENGTH];
	size_t len;

	if (!nav || !nav->page_content) {
		return FALSE;
	}

	selector_type = nav->selector[0];

	/* These types are defined by RFC 1436 as non-menus. */
	if (selector_type == '0' || selector_type == '4' || selector_type == '5' ||
	        selector_type == '6' || selector_type == '9' || selector_type == 'g' ||
	        selector_type == 'I' || selector_type == 'h') {
		return FALSE;
	}

	/* An empty selector or a selector of type '1' is always a menu. */
	if (selector_type == '\0' || selector_type == '1') {
		return TRUE;
	}

	/* Heuristic: Check for a tab character in the first line. */
	first_line_end = strchr(nav->page_content, '\n');

	if (first_line_end) {
		len = first_line_end - nav->page_content;
		if (len >= sizeof(first_line)) len = sizeof(first_line) - 1;
		strncpy(first_line, nav->page_content, len);
		first_line[len] = '\0';
	} else {
		strncpy(first_line, nav->page_content, sizeof(first_line) - 1);
		first_line[sizeof(first_line) - 1] = '\0';
	}

	/* If no tab is found, it's likely a text file, not a menu. */
	if (strchr(first_line, '\t') == NULL) {
		return FALSE;
	}

	return TRUE;
}

/* Calculates the number of lines in a text content string. */
void calculate_text_lines(AppState *state, const char *content) {
	const char *ptr = content;
	int count = 0;
	while (*ptr != '\0') {
		if (*ptr == '\n') {
			count++;
		}
		ptr++;
	}
	/* Add one for the last line if it doesn't end with a newline. */
	if (ptr > content && *(ptr - 1) != '\n') {
		count++;
	}
	state->total_content_lines = count;
}

/* Removes leading and trailing whitespace from a string in-place. */
void trim_whitespace(char* str) {
	char *end;
	/* Trim leading space */
	while (isspace((unsigned char)*str)) str++;
	if (*str == 0) return; /* All spaces? */

	/* Trim trailing space */
	end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) end--;

	/* Write new null terminator */
	end[1] = '\0';
}

/* Parses a single Gopher line into a GopherItem structure. */
BOOL parse_gopher_line(char* line, GopherItem* item, const char* current_host, int current_port) {
	char* token;
	char *line_content_start;
	char *fields[4];
	int i;

	memset(item, 0, sizeof(GopherItem));

	/* Gopher lines end with CRLF, remove the CR if present. */
	if (strlen(line) > 0 && line[strlen(line) - 1] == '\r') {
		line[strlen(line) - 1] = '\0';
	}

	/* Ignore empty lines or the end-of-listing marker ".". */
	if (line[0] == '\0' || strcmp(line, ".") == 0 || strlen(line) < 2) {
		return FALSE;
	}

	item->type = line[0];
	line_content_start = line + 1;

	for (i = 0; i < 4; ++i) {
		fields[i] = NULL;
	}

	/* Manually tokenize the line by tabs. */
	fields[0] = line_content_start;
	token = line_content_start;
	for (i = 1; i < 4; ++i) {
		token = strchr(token, '\t');
		if (token) {
			*token = '\0'; /* Null-terminate the previous field. */
			token++;
			fields[i] = token;
		} else {
			break;
		}
	}

	/* Informational items ('i') or malformed lines have no selector/host/port. */
	if (item->type == 'i' || !fields[0] || !fields[1] || !fields[2]) {
		item->is_selectable = FALSE;
		strncpy(item->display_string, line_content_start, MAX_DISPLAY_LENGTH - 1);
		item->display_string[MAX_DISPLAY_LENGTH - 1] = '\0';
		trim_whitespace(item->display_string);
		return TRUE;
	}

	strncpy(item->display_string, fields[0], MAX_DISPLAY_LENGTH - 1);
	item->display_string[MAX_DISPLAY_LENGTH - 1] = '\0';
	trim_whitespace(item->display_string);

	strncpy(item->selector, fields[1], MAX_SELECTOR_LENGTH - 1);
	item->selector[MAX_SELECTOR_LENGTH - 1] = '\0';

	strncpy(item->host, (fields[2][0] != '\0') ? fields[2] : current_host, MAX_HOST_LENGTH - 1);
	item->host[MAX_HOST_LENGTH - 1] = '\0';

	item->port = current_port;
	if (fields[3]) {
		item->port = atoi(fields[3]);
	}

	/* An item is selectable if it's a known link type and not a placeholder. */
	if (strchr("0127h", item->type) != NULL &&
	        strcmp(item->host, "null.host") != 0 &&
	        strcmp(item->host, "error.host") != 0) {
		item->is_selectable = TRUE;
	} else {
		item->is_selectable = FALSE;
	}

	return TRUE;
}

/* Processes the raw Gopher response data and populates the item list. */
void process_gopher_response(AppState* state, const char *data) {
	char *data_copy, *line;
	int capacity = 50;

	if (state->gopher_items) {
		free(state->gopher_items);
	}
	state->gopher_items = malloc(capacity * sizeof(GopherItem));
	if (!state->gopher_items) {
		die("Error: Failed to allocate memory for Gopher items.");
	}

	state->total_items = 0;
	state->selectable_items = 0;
	state->selected_index = 1;

	data_copy = malloc(strlen(data) + 1);
	if (data_copy) {
		strcpy(data_copy, data);
	}
	if (!data_copy) {
		die("Error: Failed to duplicate response data.");
	}

	line = strtok(data_copy, "\n");
	while (line != NULL) {
		GopherItem current_item;

		if (parse_gopher_line(line, &current_item, state->current_nav->host, state->current_nav->port)) {
			if (state->total_items >= capacity) {
				capacity *= 2;
				state->gopher_items = realloc(state->gopher_items, capacity * sizeof(GopherItem));
				if (!state->gopher_items) {
					die("Error: Failed to reallocate memory for Gopher items.");
				}
			}

			if (current_item.is_selectable) {
				state->selectable_items++;
				current_item.menu_index = state->selectable_items;
			}

			state->gopher_items[state->total_items] = current_item;
			state->total_items++;
		}

		line = strtok(NULL, "\n");
	}

	free(data_copy);
}

/* Handles menu navigation based on user arrow key input. */
void handle_menu_navigation(AppState *state, char input) {
	int i;
	int selected_array_idx = -1;
	int current_selectable = 0;
	int viewable_rows = state->terminal_size.ws_row > 4 ? state->terminal_size.ws_row - 4 : 0;

	if (state->selectable_items == 0) {
		return;
	}

	if (input == KEY_UP) {
		state->selected_index = (state->selected_index > 1) ? state->selected_index - 1 : state->selectable_items;
	} else if (input == KEY_DOWN) {
		state->selected_index = (state->selected_index < state->selectable_items) ? state->selected_index + 1 : 1;
	} else if (input == KEY_PGUP) {
		state->selected_index -= viewable_rows;
		if (state->selected_index < 1) state->selected_index = 1;
	} else if (input == KEY_PGDN) {
		state->selected_index += viewable_rows;
		if (state->selected_index > state->selectable_items) state->selected_index = state->selectable_items;
	}

	/* Find the array index that corresponds to the new selected menu index. */
	for (i = 0; i < state->total_items; ++i) {
		if (state->gopher_items[i].is_selectable) {
			current_selectable++;
			if (current_selectable == state->selected_index) {
				selected_array_idx = i;
				break;
			}
		}
	}

	/* Adjust the scroll offset to keep the selected item in view. */
	if (selected_array_idx != -1) {
		if (selected_array_idx < state->scroll_offset) {
			state->scroll_offset = selected_array_idx;
		} else if (selected_array_idx >= state->scroll_offset + viewable_rows) {
			state->scroll_offset = selected_array_idx - viewable_rows + 1;
		}
	}
}

/* Handles menu actions triggered by single-character input. */
void handle_menu_action(AppState *state, char input) {
	int i;
	if (input == KEY_ENTER || input == KEY_CARRIAGE_RETURN) {
		for (i = 0; i < state->total_items; ++i) {
			if (state->gopher_items[i].is_selectable && state->gopher_items[i].menu_index == state->selected_index) {
				GopherItem selected = state->gopher_items[i];
				if (selected.type == '7') {
					handle_search_prompt(state, &selected);
				} else {
					navigate_to(state, selected.host, selected.port, selected.selector);
				}
				return; /* Action handled */
			}
		}
	} else if (input == 'b' || input == KEY_BACKSPACE) {
		navigate_back(state);
	} else if (input == 'f') {
		navigate_forward(state);
	} else if (input == 'r') {
		if (state->current_nav->page_content) {
			free(state->current_nav->page_content);
			state->current_nav->page_content = NULL;
		}
	} else if (input == 'a') {
		show_about_screen(state);
	} else if (input == 'o') {
		handle_open_prompt(state);
	} else if (input == 'q') {
		state->is_running = FALSE;
	}
}

/* Manages user interaction for a Gopher menu screen. */
BOOL handle_gopher_menu_interaction(AppState* state) {
	char input_buf[3];
	ssize_t bytes_read;
	fd_set read_fds;
	struct timeval tv;

	draw_gopher_menu(state);

	while (state->is_running) {
		if (g_resize_pending) {
			ioctl(STDOUT_FILENO, TIOCGWINSZ, &state->terminal_size);
			draw_gopher_menu(state);
			g_resize_pending = 0;
			continue;
		}

		FD_ZERO(&read_fds);
		FD_SET(STDIN_FILENO, &read_fds);
		tv.tv_sec = 0;
		tv.tv_usec = 100000; /* 100ms timeout */

		if (select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &tv) > 0) {
			if (FD_ISSET(STDIN_FILENO, &read_fds)) {
				bytes_read = read(STDIN_FILENO, input_buf, sizeof(input_buf));
				if (bytes_read <= 0) continue;

				/* Handle 3-byte ANSI escape codes for arrow keys. */
				if (bytes_read == 3 && input_buf[0] == KEY_ESC && input_buf[1] == '[') {
					handle_menu_navigation(state, input_buf[2]);
					draw_gopher_menu(state);
				} else if (bytes_read == 1) {
					handle_menu_action(state, input_buf[0]);
					return state->is_running; /* Return to main loop to process state change. */
				}
			}
		}
	}
	return state->is_running;
}

/* Manages user interaction for a text viewer screen. */
BOOL handle_text_viewer_interaction(AppState* state) {
	char input_buf[3];
	ssize_t bytes_read;
	int viewable_rows;
	fd_set read_fds;
	struct timeval tv;

	draw_text_viewer(state, state->current_nav->page_content);

	while (state->is_running) {
		viewable_rows = state->terminal_size.ws_row > 4 ? state->terminal_size.ws_row - 4 : 1;
		calculate_text_lines(state, state->current_nav->page_content);

		/* Clamp scroll position to prevent overscrolling. */
		if (state->text_scroll_line > state->total_content_lines - viewable_rows) {
			state->text_scroll_line = state->total_content_lines - viewable_rows;
		}
		if (state->text_scroll_line < 0) {
			state->text_scroll_line = 0;
		}

		if (g_resize_pending) {
			ioctl(STDOUT_FILENO, TIOCGWINSZ, &state->terminal_size);
			draw_text_viewer(state, state->current_nav->page_content);
			g_resize_pending = 0;
			continue;
		}

		FD_ZERO(&read_fds);
		FD_SET(STDIN_FILENO, &read_fds);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;

		if (select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &tv) > 0) {
			if (FD_ISSET(STDIN_FILENO, &read_fds)) {
				bytes_read = read(STDIN_FILENO, input_buf, sizeof(input_buf));
				if (bytes_read <= 0) continue;

				if (bytes_read == 3 && input_buf[0] == KEY_ESC && input_buf[1] == '[') {
					char key = input_buf[2];
					if (key == KEY_UP && state->text_scroll_line > 0) {
						state->text_scroll_line--;
					} else if (key == KEY_DOWN) {
						state->text_scroll_line++;
					} else if (key == KEY_PGUP) {
						state->text_scroll_line -= viewable_rows;
						if (state->text_scroll_line < 0) state->text_scroll_line = 0;
					} else if (key == KEY_PGDN) {
						state->text_scroll_line += viewable_rows;
					}
					draw_text_viewer(state, state->current_nav->page_content);
				} else if (bytes_read == 1) {
					char c = input_buf[0];
					if (c == 'b' || c == KEY_BACKSPACE) {
						navigate_back(state);
					} else if (c == 'f') {
						navigate_forward(state);
					} else if (c == 'r') {
						if (state->current_nav->page_content) {
							free(state->current_nav->page_content);
							state->current_nav->page_content = NULL;
						}
					} else if (c == 'a') {
						show_about_screen(state);
						draw_text_viewer(state, state->current_nav->page_content); /* Redraw after about screen */
						continue;
					} else if (c == 'o') {
						handle_open_prompt(state);
						/* The main loop will handle redrawing */
					} else if (c == 'q') {
						state->is_running = FALSE;
					}
					return state->is_running;
				}
			}
		}
	}
	return state->is_running;
}

/* Prompts the user for a search query. */
void handle_search_prompt(AppState *state, const GopherItem *item) {
	char query[MAX_SELECTOR_LENGTH] = {0};
	char full_selector[MAX_SELECTOR_LENGTH * 2 + 2]; /* selector + \t + query + \0 */
	int rows = state->terminal_size.ws_row;
	int start_col = (state->terminal_size.ws_col - MAX_CONTENT_DISPLAY_WIDTH)/2;
	int i = 0;
	char c;

	if (start_col < 1) start_col = 1;

	clear_line(rows, state->terminal_size.ws_col);
	move_cursor(rows, start_col);
	printf("%sSearch query: %s", FOOTER_COLOR, COLOR_RESET);
	move_cursor(rows, start_col + strlen("Search query: "));
	set_cursor_visibility(1);
	fflush(stdout);

	/* Simple blocking read loop for the prompt */
	while (read(STDIN_FILENO, &c, 1) > 0) {
		if (c == KEY_ENTER || c == KEY_CARRIAGE_RETURN) {
			break;
		} else if (c == KEY_BACKSPACE || c == 8 /* Backspace on some terminals */) {
			if (i > 0) {
				i--;
				move_cursor(rows, start_col + strlen("Search query: ") + i);
				printf("\b \b"); /* Erase character safely */
				fflush(stdout);
			}
		} else if (c == KEY_ESC || c == 'q') {
			i = 0; /* Cancel search */
			break;
		} else if (isprint(c) && i < MAX_SELECTOR_LENGTH - 1) {
			query[i++] = c;
			printf("%c", c);
			fflush(stdout);
		}
	}
	query[i] = '\0';

	set_cursor_visibility(0);
	clear_line(rows, state->terminal_size.ws_col);

	if (i > 0) { /* If user entered a query */
		if ((strlen(item->selector) + strlen(query) + 2) < sizeof(full_selector)) {
			sprintf(full_selector, "%s\t%s", item->selector, query);
			navigate_to(state, item->host, item->port, full_selector);
		} else {
			/* Handle the case where the combined string is too long. */
			printf("%sError: Search query is too long.%s\n", ERROR_COLOR, COLOR_RESET);
		}
	}
}

/* Prompts the user for a Gopher URL to open. */
void handle_open_prompt(AppState *state) {
	char url_input[MAX_URL_INPUT_LENGTH] = {0};
	char new_host[MAX_HOST_LENGTH];
	int new_port;
	char new_selector[MAX_SELECTOR_LENGTH];
	int rows = state->terminal_size.ws_row;
	int start_col = (state->terminal_size.ws_col - MAX_CONTENT_DISPLAY_WIDTH) / 2;
	int i = 0;
	char c;

	if (start_col < 1) start_col = 1;

	for (;;) { /* Loop until valid URL or cancel */
		i = 0;
		url_input[0] = '\0';

		clear_line(rows, state->terminal_size.ws_col);
		move_cursor(rows, start_col);
		printf("%sOpen URL: %s", FOOTER_COLOR, COLOR_RESET);
		move_cursor(rows, start_col + strlen("Open URL: "));
		set_cursor_visibility(1);
		fflush(stdout);

		while (read(STDIN_FILENO, &c, 1) > 0) {
			if (c == KEY_ENTER || c == KEY_CARRIAGE_RETURN) {
				break;
			} else if (c == KEY_BACKSPACE || c == 8) {
				if (i > 0) {
					i--;
					move_cursor(rows, start_col + strlen("Open URL: ") + i);
					printf("\b \b");
					fflush(stdout);
				}
			} else if (c == KEY_ESC) {
				i = 0; /* Cancel */
				break;
			} else if (isprint(c) && i < MAX_URL_INPUT_LENGTH - 1) {
				url_input[i++] = c;
				printf("%c", c);
				fflush(stdout);
			}
		}
		url_input[i] = '\0';
		set_cursor_visibility(0);

		if (i == 0) { /* User cancelled */
			clear_line(rows, state->terminal_size.ws_col);
			return;
		}

		if (parse_gopher_address(url_input, new_host, &new_port, new_selector)) {
			navigate_to(state, new_host, new_port, new_selector);
			return; /* Success */
		} else {
			clear_line(rows, state->terminal_size.ws_col);
			move_cursor(rows, start_col);
			printf("%sError: Invalid Gopher address format. Press any key.%s", ERROR_COLOR, COLOR_RESET);
			fflush(stdout);
			read(STDIN_FILENO, &c, 1); /* Wait for key press */
		}
	}
}

/* Formats the current Gopher URL into a string. */
void get_current_url(const NavigationState* nav, char* buffer, size_t size) {
	size_t required_size;

	if (nav->selector[0] == '\0' || (nav->selector[0] == '1' && nav->selector[1] == '\0')) {
		required_size = strlen("gopher://") + strlen(nav->host) + 1 + 5 + 1;
		if (required_size < size) {
			sprintf(buffer, "gopher://%s:%d/", nav->host, nav->port);
		} else {
			buffer[0] = '\0';
		}
	} else {
		required_size = strlen("gopher://") + strlen(nav->host) + 1 + 5 + 1 + strlen(nav->selector) + 1;
		if (required_size < size) {
			sprintf(buffer, "gopher://%s:%d/%s", nav->host, nav->port, nav->selector);
		} else {
			buffer[0] = '\0';
		}
	}
}

/* Draws the application header with the current URL. */
void draw_header(const AppState* state) {
	char url_buffer[MAX_URL_INPUT_LENGTH];
	char header_background[MAX_CONTENT_DISPLAY_WIDTH + 1];

	get_current_url(state->current_nav, url_buffer, sizeof(url_buffer));

	/* Create a string of spaces for the background. */
	memset(header_background, ' ', MAX_CONTENT_DISPLAY_WIDTH);
	header_background[MAX_CONTENT_DISPLAY_WIDTH] = '\0';

	/* Print the colored background bar, centered. */
	printf("%s%s", HEADER_BG, HEADER_FG);
	print_centered_string(header_background, 1, state->terminal_size.ws_col);

	/* Print the URL text on top of the background. */
	print_centered_string(url_buffer, 1, state->terminal_size.ws_col);
	printf("%s", COLOR_RESET);

	move_cursor(2, 1); /* Move cursor below header for content. */
	fflush(stdout);
}

/* Draws the Gopher menu to the terminal screen. */
void draw_gopher_menu(AppState* state) {
	int available_rows;
	int i;
	int item_on_screen_count = 0;
	int start_col;
	char display_buf[MAX_DISPLAY_LENGTH + 20];
	const char* color;
	BOOL is_selected;

	clear_terminal();
	draw_header(state);

	available_rows = state->terminal_size.ws_row > 4 ? state->terminal_size.ws_row - 4 : 0;
	start_col = (state->terminal_size.ws_col - MAX_CONTENT_DISPLAY_WIDTH) / 2 + 1;
	if (start_col < 1) start_col = 1;

	for (i = state->scroll_offset; i < state->total_items && item_on_screen_count < available_rows; ++i) {
		is_selected = state->gopher_items[i].is_selectable && (state->gopher_items[i].menu_index == state->selected_index);

		if (state->gopher_items[i].is_selectable) {
			if (strlen(state->gopher_items[i].display_string) + 3 < sizeof(display_buf)) {
				sprintf(display_buf, "%s%s", is_selected ? "->" : "  ", state->gopher_items[i].display_string);
			} else {
				strncpy(display_buf, state->gopher_items[i].display_string, sizeof(display_buf) - 1);
				display_buf[sizeof(display_buf) - 1] = '\0';
			}
		} else {
			if (strlen(state->gopher_items[i].display_string) + 3 < sizeof(display_buf)) {
				sprintf(display_buf, "  %s", state->gopher_items[i].display_string);
			} else {
				strncpy(display_buf, state->gopher_items[i].display_string, sizeof(display_buf) - 1);
				display_buf[sizeof(display_buf) - 1] = '\0';
			}
		}

		color = get_gopher_item_color(state->gopher_items[i].type, is_selected);
		printf("%s", color);
		print_string_at(display_buf, 4 + item_on_screen_count, start_col);
		printf("%s", COLOR_RESET);
		item_on_screen_count++;
	}
	fflush(stdout);
}

/* Draws the current text content to the terminal screen. */
void draw_text_viewer(AppState* state, const char *content) {
	int available_rows;
	const char *ptr = content;
	int start_col;
	int lines_to_skip;
	int drawn_lines = 0;

	clear_terminal();
	draw_header(state);

	available_rows = state->terminal_size.ws_row > 4 ? state->terminal_size.ws_row - 4 : 0;
	start_col = (state->terminal_size.ws_col - MAX_CONTENT_DISPLAY_WIDTH)/2 + 1;
	if (start_col < 1) start_col = 1;

	printf("%s", TEXT_COLOR);

	/* Skip lines to the current scroll offset. */
	lines_to_skip = state->text_scroll_line;
	while (*ptr != '\0' && lines_to_skip > 0) {
		const char *newline = strchr(ptr, '\n');
		if (newline == NULL) { /* Reached end of content */
			ptr = content + strlen(content);
			break;
		}
		ptr = newline + 1;
		lines_to_skip--;
	}

	/* Draw content from the new starting point, line by line. */
	while (*ptr != '\0' && drawn_lines < available_rows) {
		const char* next_newline = strchr(ptr, '\n');
		size_t line_length = (next_newline != NULL) ? (size_t)(next_newline - ptr) : strlen(ptr);
		char temp_line[MAX_CONTENT_DISPLAY_WIDTH + 1];

		/* Truncate line if it's too long for the display width. */
		size_t copy_len = line_length > MAX_CONTENT_DISPLAY_WIDTH ? MAX_CONTENT_DISPLAY_WIDTH : line_length;
		strncpy(temp_line, ptr, copy_len);
		temp_line[copy_len] = '\0';

		print_string_at(temp_line, 4 + drawn_lines, start_col);

		drawn_lines++;
		if (next_newline != NULL) {
			ptr = next_newline + 1;
		} else {
			break; /* End of content */
		}
	}

	printf("%s", COLOR_RESET);
	fflush(stdout);
}

void show_about_screen(const AppState* state) {
	char c;
	char header_background[MAX_CONTENT_DISPLAY_WIDTH + 1];

	const char *about_body[] = {
		"    Welcome to Tocaia %s!",
		"        \\`~'/",
		"        (o o)",
		"       / \\ / \\",
		"          \"",
		"",
		"Shortcuts:",
		"    Arrows: Navigate",
		"      Enter: Select",
		"        b: Back",
		"        f: Forward",
		"        o: Open URL",
		"        r: Reload",
		"        a: About",
		"        q: Quit",
		NULL
	};

	int body_lines = 0;
	int max_width = 0;
	int start_row;
	int start_col;
	char version_info[50];
	int i;

	while (about_body[body_lines] != NULL) {
		int len = strlen(about_body[body_lines]);
		if (len > max_width) {
			max_width = len;
		}
		body_lines++;
	}

	clear_terminal();
	memset(header_background, ' ', MAX_CONTENT_DISPLAY_WIDTH);
	header_background[MAX_CONTENT_DISPLAY_WIDTH] = '\0';

	printf("%s%s", HEADER_BG, HEADER_FG);
	print_centered_string(header_background, 1, state->terminal_size.ws_col);
	print_centered_string("About Tocaia", 1, state->terminal_size.ws_col);
	printf(COLOR_RESET);

	move_cursor(2, 1);

	start_row = (state->terminal_size.ws_row - body_lines) / 2;
	if (start_row < 3) start_row = 3;

	start_col = (state->terminal_size.ws_col - max_width) / 2;
	if (start_col < 1) start_col = 1;

	sprintf(version_info, "Welcome to Tocaia %s!", PROGRAM_VERSION);

	for (i = 0; i < body_lines; ++i) {
		if (i == 0) {
			printf("%s", DIRECTORY_COLOR);
			print_string_at(version_info, start_row + i, start_col);
		} else if (i >= 1 && i <= 4) {
			printf("%s", BINARY_COLOR);
			print_string_at(about_body[i], start_row + i, start_col);
		} else if (i == 6) {
			printf("%s", DIRECTORY_COLOR);
			print_string_at(about_body[i], start_row + i, start_col);
		} else {
			printf("%s", TEXT_COLOR);
			print_string_at(about_body[i], start_row + i, start_col);
		}
	}
	printf("%s", COLOR_RESET);

	fflush(stdout);

	/* Wait for a key */
	while (read(STDIN_FILENO, &c, 1) != 1);
}

/* Creates and initializes a new NavigationState node. */
NavigationState* create_nav_state(const char *host, int port, const char *selector) {
	NavigationState *new_state = malloc(sizeof(NavigationState));
	if (!new_state) {
		die("Error: Failed to allocate memory for navigation state.");
	}

	strncpy(new_state->host, host, sizeof(new_state->host) - 1);
	new_state->host[sizeof(new_state->host)-1] = '\0';
	strncpy(new_state->selector, selector, sizeof(new_state->selector) - 1);
	new_state->selector[sizeof(new_state->selector)-1] = '\0';
	new_state->port = port;
	new_state->page_content = NULL;
	new_state->prev = NULL;
	new_state->next = NULL;
	return new_state;
}

/* Frees all `NavigationState` nodes in the forward history. */
void free_forward_history(NavigationState *current_state) {
	NavigationState *temp;
	NavigationState *forward_node = current_state ? current_state->next : NULL;

	while (forward_node) {
		temp = forward_node;
		forward_node = temp->next;
		if (temp->page_content) {
			free(temp->page_content);
		}
		free(temp);
	}
	if (current_state) {
		current_state->next = NULL;
	}
}

/* Frees the entire navigation history linked list. */
void free_navigation_history(NavigationState *current_state) {
	NavigationState *head = current_state;
	NavigationState *temp;
	if (!head) {
		return;
	}

	/* Find the head of the list. */
	while (head->prev) {
		head = head->prev;
	}

	/* Traverse from the head and free each node. */
	while (head) {
		temp = head;
		head = temp->next;
		if (temp->page_content) {
			free(temp->page_content);
		}
		free(temp);
	}
}

/* Navigates to a new Gopher address and adds it to the history. */
void navigate_to(AppState *state, const char *host, int port, const char *selector) {
	NavigationState *new_state = create_nav_state(host, port, selector);

	if (state->current_nav) {
		free_forward_history(state->current_nav);
		state->current_nav->next = new_state;
		new_state->prev = state->current_nav;
	}
	state->current_nav = new_state;

	/* Reset view state for the new page. */
	state->selected_index = 1;
	state->scroll_offset = 0;
	state->text_scroll_line = 0;
}

/* Moves the navigation back one step in the history. */
void navigate_back(AppState *state) {
	if (state->current_nav && state->current_nav->prev) {
		state->current_nav = state->current_nav->prev;
		state->selected_index = 1;
		state->scroll_offset = 0;
		state->text_scroll_line = 0;
	}
}

/* Moves the navigation forward one step in the history. */
void navigate_forward(AppState *state) {
	if (state->current_nav && state->current_nav->next) {
		state->current_nav = state->current_nav->next;
		state->selected_index = 1;
		state->scroll_offset = 0;
		state->text_scroll_line = 0;
	}
}

/* Sets the terminal to "raw" mode for direct key input handling. */
void setup_terminal_for_app(void) {
	struct termios raw;

	tcgetattr(STDIN_FILENO, &g_original_termios);
	raw = g_original_termios;
	/* ICANON: disable canonical mode.
	 * ECHO: disable echoing input characters.
	 * ISIG: disable signal-generating characters (e.g., Ctrl-C). */
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

	signal(SIGWINCH, handle_resize_signal);
	signal(SIGINT, handle_sigint_signal);
	set_cursor_visibility(0); /* Hide cursor */
}

/* Restores the terminal to its original state.*/
void restore_terminal(void) {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_original_termios);
	clear_terminal();
	move_cursor(1,1);
	set_cursor_visibility(1); /* Show cursor */
	printf("%s", COLOR_RESET); /* Reset any lingering colors. */
}

/* Signal handler for terminal window resizing (SIGWINCH). */
void handle_resize_signal(int sig) {
	(void)sig; /* Unused parameter */
	g_resize_pending = 1;
}

/* Signal handler for interrupt signals (SIGINT / Ctrl+C). */
void handle_sigint_signal(int sig) {
	(void)sig; /* Unused parameter */
	restore_terminal();
	exit(EXIT_SUCCESS);
}

/* Controls the visibility of the terminal cursor using ANSI escape codes. */
void set_cursor_visibility(int visible) {
	printf("\033[?25%c", visible ? 'h' : 'l');
	fflush(stdout);
}

/* Clears the entire terminal screen. */
void clear_terminal(void) {
	printf("\033[H\033[J");
	fflush(stdout);
}

/* Clears a single line in the terminal by overwriting with spaces.*/
void clear_line(int row, int term_width) {
	char *clear_str = malloc(term_width + 1);
	if (clear_str) {
		memset(clear_str, ' ', term_width);
		clear_str[term_width] = '\0';
		move_cursor(row, 1);
		printf("%s", clear_str);
		free(clear_str);
	}
}

void move_cursor(int row, int col) {
	printf("\033[%d;%dH", row, col);
}

void print_string_at(const char *str, int row, int col) {
	move_cursor(row, col);
	printf("%s", str);
}

void print_centered_string(const char *str, int row, int term_width) {
	int len = strlen(str);
	int start_col = (term_width - len) / 2;
	if (start_col < 1) start_col = 1;
	print_string_at(str, row, start_col);
}

/* Helper function that ensures all bytes from a buffer are written. */
int write_all(int fd, const char* buffer, size_t len) {
	size_t bytes_sent = 0;
	ssize_t n_sent;

	while (bytes_sent < len) {
		n_sent = write(fd, buffer + bytes_sent, len - bytes_sent);
		if (n_sent == -1) {
			if (errno == EINTR) {
				continue; /* Interrupted by a signal, try again */
			}
			return -1; /* A real error occurred */
		}
		bytes_sent += n_sent;
	}
	return bytes_sent;
}

/* Connects to a Gopher server and sends a selector request */
int connect_and_send_request(const char *host, int port, const char *selector) {
	int sock = -1;
	struct hostent *he;
	struct sockaddr_in server_addr;
	char request[MAX_SELECTOR_LENGTH + 3]; /* selector + CRLF + null */
	char **p;
	size_t request_len;

	he = gethostbyname(host);
	if (he == NULL) {
		die("Error: Failed to resolve host");
	}

	/* Iterates through the list of addresses returned by gethostbyname */
	for (p = he->h_addr_list; *p != 0; p++) {
		/* Creates a socket for each address */
		if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			perror("socket");
			continue;
		}

		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(port);
		memcpy(&server_addr.sin_addr, *p, sizeof(struct in_addr));

		/* Tries to connect to the server */
		if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != -1) {
			/* Connection successful! Exits the loop. */
			break;
		}

		/* If the connection failed, closes the current socket and tries the next address. */
		close(sock);
		sock = -1;
	}

	if (sock == -1) {
		die("Error: Could not connect to host");
	}

	request_len = strlen(selector) + strlen(CRLF);
	if (request_len >= sizeof(request)) {
		die("Error: The request is too long.");
	}
	sprintf(request, "%s%s", selector, CRLF);

	if (write_all(sock, request, request_len) != request_len) {
		die("Error: Failed to send the request.");
	}

	return sock;
}

/* Receives all data from a socket until the connection is closed */
char *receive_gopher_data(int sock) {
	size_t buffer_size = INITIAL_BUFFER_SIZE;
	char *buffer = (char*)malloc(buffer_size);
	size_t total_bytes = 0;
	ssize_t bytes_received;
	char *new_buffer;

	if (!buffer) {
		die("Error: Failed to allocate memory for receive buffer.");
	}

	while ((bytes_received = read(sock, buffer + total_bytes, buffer_size - total_bytes - 1)) > 0) {
		total_bytes += bytes_received;
		if (total_bytes >= buffer_size - 1) {
			buffer_size *= 2;
			new_buffer = (char*)realloc(buffer, buffer_size);
			if (!new_buffer) {
				die("Error: Failed to reallocate memory for receive buffer.");
			}
			buffer = new_buffer;
		}
	}

	if (bytes_received < 0) {
		die("Error: Failed to read from socket.");
	}

	buffer[total_bytes] = '\0';
	return buffer;
}

void die(const char *message) {
	fprintf(stderr, "%s\n", message);
	exit(EXIT_FAILURE);
}

void show_help(void) {
	printf("Usage: tocaia [gopher_address]\n");
	printf("A command-line Gopher client.\n\n");
	printf("Arguments:\n");
	printf("  gopher_address  The Gopher server address. E.g., 'gopher.example.org', 'gopher://ex.org:70/1/dir'.\n\n");
	printf("Options:\n");
	printf("  -h, --help     Display this help message and exit.\n");
	printf("  -v, --version  Display program version and exit.\n");
}

void show_version(void) {
	printf("Tocaia %s\n", PROGRAM_VERSION);
}

/* Gets a human-readable description for a Gopher item type. */
const char* get_gopher_type_description(char type) {
	switch (type) {
	case '0':
		return "<TEXT>";
	case '1':
		return "<DIR>";
	case '2':
		return "<CSO>";
	case '3':
		return "<ERROR>";
	case '4':
		return "<BINHEX>";
	case '5':
		return "<DOS>";
	case '6':
		return "<UUENC>";
	case '7':
		return "<SEARCH>";
	case '8':
		return "<TELNET>";
	case '9':
		return "<BINARY>";
	case 'g':
		return "<GIF>";
	case 'h':
		return "<HTML>";
	case 'i':
		return ""; /* Informational items have no prefix */
	default:
		return "<UNKN>";
	}
}

/* Gets the ANSI color code for a Gopher item. */
const char* get_gopher_item_color(char type, BOOL selected) {
	if (selected) {
		return SELECTED_ITEM_COLOR;
	}
	switch (type) {
	case '0':
		return TEXT_COLOR;
	case '1':
		return DIRECTORY_COLOR;
	case '2':
		return CSO_COLOR;
	case '3':
		return ERROR_COLOR;
	case '4':
	case '5':
	case '6':
	case '9':
		return BINARY_COLOR;
	case '7':
		return SEARCH_COLOR;
	case '8':
		return TELNET_COLOR;
	case 'g':
		return GIF_COLOR;
	case 'h':
		return HTML_COLOR;
	case 'i':
		return INFO_COLOR;
	default:
		return UNKNOWN_COLOR;
	}
}

/* Parses a Gopher address string into its host, port, and selector components. */
BOOL parse_gopher_address(const char *address, char *host_out, int *port_out, char *selector_out) {
	char temp_address[MAX_URL_INPUT_LENGTH];
	const char *parse_ptr;
	char *host_port_str, *port_str, *first_slash;

	if (!address || strlen(address) == 0) {
		return FALSE;
	}

	strncpy(temp_address, address, sizeof(temp_address) - 1);
	temp_address[sizeof(temp_address) - 1] = '\0';

	if (strncmp(temp_address, "gopher://", 9) == 0) {
		parse_ptr = temp_address + 9;
	} else {
		parse_ptr = temp_address;
	}

	host_port_str = (char*)parse_ptr;

	*port_out = 70;
	strncpy(selector_out, "", MAX_SELECTOR_LENGTH);
	selector_out[MAX_SELECTOR_LENGTH - 1] = '\0';

	first_slash = strchr(parse_ptr, '/');
	if (first_slash) {
		*first_slash = '\0';
		strncpy(selector_out, first_slash + 1, MAX_SELECTOR_LENGTH - 1);
		selector_out[MAX_SELECTOR_LENGTH - 1] = '\0';
	}

	port_str = strrchr(host_port_str, ':');
	if (port_str) {
		*port_str = '\0';
		if (strlen(port_str + 1) > 0) {
			*port_out = atoi(port_str + 1);
		} else {
			return FALSE;
		}
	}

	if (strlen(host_port_str) == 0) {
		return FALSE;
	}

	if (strchr(host_port_str, ' ') || strchr(host_port_str, '\t') || strchr(host_port_str, '\n')) {
		return FALSE;
	}
	if (!strchr(host_port_str, '.') && !isdigit(host_port_str[0])) {
		return FALSE;
	}

	strncpy(host_out, host_port_str, MAX_HOST_LENGTH - 1);
	host_out[MAX_HOST_LENGTH - 1] = '\0';

	if (*port_out <= 0 || *port_out > 65535) {
		return FALSE;
	}

	return TRUE;
}
