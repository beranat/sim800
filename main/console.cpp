// vim: tabstop=4 shiftwidth=4 noexpandtab colorcolumn=120 :
// This file is part of the Sim800 (https://github.com/beranat/sim800).
// Copyright (c) 2021 Anatoly L. Berenblit.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
#include <esp_log.h>
#include <esp_console.h>
#include <esp_vfs_dev.h>
#include <driver/uart.h>
#include <stdio.h>
#include <string.h>

#include <linenoise/linenoise.h>

//#include <argtable3/argtable3.h>
//#include "cmd_decl.h"

#include "main.hpp"
#include "sdkconfig.h"

#include "console.hpp"

static constexpr const char *MODULE = "console";

static constexpr unsigned int CONSOLE_UART_BAUDRATE = 115200;
static constexpr unsigned int CONSOLE_UART_BUFFER_RX = 256;
static constexpr unsigned int CONSOLE_COMMANDLINE_ARGS = 8;
static constexpr unsigned int CONSOLE_COMMANDLINE_HISTORY = 32;
static constexpr unsigned int CONSOLE_COMMANDLINE_LENGTH = CONSOLE_UART_BUFFER_RX - 8;
static constexpr const char  *CONSOLE_PROMPT_SIMPLE = "[console]$ ";

#if CONFIG_LOG_COLORS
static constexpr const char *CONSOLE_PROMPT = LOG_RESET_COLOR "[" LOG_COLOR(LOG_COLOR_CYAN) "console" LOG_RESET_COLOR
		"]$ ";
static constexpr const char *CONSOLE_OK = "OK";
static constexpr const char *CONSOLE_ERROR = "Error:";
#else
static constexpr const char *CONSOLE_PROMPT = CONSOLE_PROMPT;
static constexpr const char *CONSOLE_OK = LOG_COLOR_I "OK" LOG_RESET_COLOR;
static constexpr const char *CONSOLE_ERROR = LOG_COLOR_E "Error:" LOG_RESET_COLOR;
#endif

esp_err_t consoleInit(void) noexcept {
	ESP_LOGI(MODULE, "Console init");

	fflush(stdout);
	fsync(fileno(stdout));
	setvbuf(stdin, NULL, _IONBF, 0);

	esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
	esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

	const uart_config_t uart_config = {
		.baud_rate = CONSOLE_UART_BAUDRATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.rx_flow_ctrl_thresh = 122,
		.source_clk = UART_SCLK_REF_TICK
	};

	ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, CONSOLE_UART_BUFFER_RX, 0, 0, NULL, 0));
	ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));

	esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

	/* Initialize the console */
	esp_console_config_t console_config = {
		.max_cmdline_length = CONSOLE_COMMANDLINE_LENGTH,
		.max_cmdline_args = CONSOLE_COMMANDLINE_ARGS,
#if CONFIG_LOG_COLORS
		.hint_color = atoi(LOG_COLOR_PURPLE),
		.hint_bold = 0
#endif
	};
	ESP_ERROR_CHECK(esp_console_init(&console_config));
	esp_console_register_help_command();

	linenoiseSetMultiLine(1);

	linenoiseSetCompletionCallback(&esp_console_get_completion);
	linenoiseSetHintsCallback((linenoiseHintsCallback *) &esp_console_get_hint);

	linenoiseHistorySetMaxLen(CONSOLE_COMMANDLINE_HISTORY);
	linenoiseAllowEmpty(false);
	return ESP_OK;
}


esp_err_t consoleAdd(const char *name, const char *description, CommandFunction fn) noexcept {
	const esp_console_cmd_t cmd = {
		.command = name,
		.help = (nullptr != description && 0 != *description)?description:"No description",
		.hint = NULL,
		.func = fn,
		.argtable = nullptr
	};
	return esp_console_cmd_register(&cmd);
}

void consoleLoop() noexcept {
	const char *prompt = CONSOLE_PROMPT;

	if (linenoiseProbe()) {
		printf("\n"
			   "Your terminal application does not support ANSI- sequences.\n"
			   "Colors, line editing and history features are disabled.\n");
		linenoiseSetDumbMode(1);
		prompt = CONSOLE_PROMPT_SIMPLE;
	}

	while (true) {
		char *line = linenoise(prompt);
		if (nullptr == line || 0 == *line)
			continue;

		linenoiseHistoryAdd(line);

		int ret = 0;
		const esp_err_t err = esp_console_run(line, &ret);
		switch (err) {
			case ESP_ERR_NOT_FOUND:
				printf("Unrecognized command\n");
				break;
			case ESP_ERR_INVALID_ARG:
				printf("Empty command\n");
				break;
			case ESP_OK:
				if (0 != ret)
					printf("%s %s(%d)\n", CONSOLE_ERROR, esp_err_to_name(ret), ret);
				else
					printf("%s\n", CONSOLE_OK);
				break;
			default:
				printf("Internal error: %s\n", esp_err_to_name(err));
				break;
		}
		linenoiseFree(line);
	}

	fatalError("Console stopped", MODULE);
}
