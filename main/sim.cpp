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
#include <cstring>
#include <string>
#include <exception>
#include <system_error>

#include <esp_log.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <driver/uart.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "main.hpp"
#include "console.hpp"
#include "sdkconfig.h"

#include "sim.hpp"

constexpr const char *MODULE = "sim";
constexpr const bool verbose = true;

constexpr unsigned int resetDelayEnableMs = 500;
// Pull down PWRKEY for more than 1 second according to manual requirements
constexpr unsigned int powerKeyDelayEnableMs = 1250;
constexpr unsigned int powerKeyDelayDisableMs = 2000;
static_assert(powerKeyDelayEnableMs + powerKeyDelayDisableMs > 2900, ""); // UART will be ready in 2.9 seconds

constexpr unsigned int SIM800_UART_BUFFER_RX = 256;
constexpr unsigned int SIM800_UART_BUFFER_TX = 0;

static void recvReceiver(void *ptr) noexcept;
constexpr size_t recvStackSize = configMINIMAL_STACK_SIZE + 1024*2;
constexpr UBaseType_t recvPriority = tskIDLE_PRIORITY + 1;
TaskHandle_t recvHandle = nullptr;

static int sendCommand(int argc, char **argv) {
	std::string command = "AT";
	for (int i = 1; i < argc; ++i) {
		if (1 != i)
			command.append(",");
		command.append(argv[i]);
	}
	command.append("\r\n");
	return simSend(command.c_str(), command.length());
}

bool recvParseLine(const char *line, ptrdiff_t length) noexcept {
	if (verbose)
		printf("%s >> %s\n", MODULE, line);

	return true;
}

void recvReceiver(void *ptr) noexcept {
	uart_flush_input(CONFIG_SIM800_UART_PORT);

	constexpr TickType_t poolPeriod = pdMS_TO_TICKS(250);

	char buffer[128], *bufPtr = nullptr;
	size_t bufLength = 0;

	do {
		if (nullptr == bufPtr) {
			ESP_LOGI(MODULE, "Flush receiver");
			uart_flush_input(CONFIG_SIM800_UART_PORT);
			bufPtr = buffer;
			bufLength = sizeof(buffer);
		}


		const int recvLen = uart_read_bytes(CONFIG_SIM800_UART_PORT, reinterpret_cast<uint8_t *>(bufPtr), bufLength-1,
											poolPeriod);
		if (recvLen <= 0) {
			if (recvLen < 0) {
				ESP_LOGE(MODULE, "Receiver error, flush data");
				bufPtr = nullptr;
			}
			continue;
		}
		bufPtr[recvLen] = 0;

		// OPERATE LINE(s)
		for (char *line = buffer; nullptr != line;) {
			char *end = strpbrk(line, "\r\n");
			if (nullptr == end) {
				const size_t length = strlen(line);
				memmove(buffer, line, sizeof(*buffer)*length);
				bufPtr = buffer + length;
				bufLength = sizeof(buffer) - length;
				line = nullptr;
			} else {
				*end++ = 0;
				if (0 != *line && !recvParseLine(line, line - end)) {
					ESP_LOGE(MODULE, "Receiver parse error, flush data");
					bufPtr = nullptr;
				}
				line = end;
			}
		}

	} while (true);

	fatalError(ESP_FAIL, "Receiver stopped", MODULE);
}

esp_err_t simInit() noexcept try {
	{
		gpio_config_t config = {
			.pin_bit_mask = BIT64(CONFIG_SIM800_POWER_GPIO) | BIT64(CONFIG_SIM800_RESET_GPIO) | BIT64(CONFIG_SIM800_POWERKEY_GPIO),
			.mode = GPIO_MODE_OUTPUT,
			.pull_up_en = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type = GPIO_INTR_DISABLE
		};
		ESP_ERROR_CHECK(gpio_config(&config));
	}

	ESP_LOGI(MODULE, "Chip init");
	gpio_set_level(static_cast<gpio_num_t>(CONFIG_SIM800_POWER_GPIO), 1);
	gpio_set_level(static_cast<gpio_num_t>(CONFIG_SIM800_RESET_GPIO), 0);
	gpio_set_level(static_cast<gpio_num_t>(CONFIG_SIM800_POWERKEY_GPIO), 1);

	vTaskDelay(pdMS_TO_TICKS(resetDelayEnableMs));
	gpio_set_level(static_cast<gpio_num_t>(CONFIG_SIM800_RESET_GPIO), 1);
	gpio_set_level(static_cast<gpio_num_t>(CONFIG_SIM800_POWERKEY_GPIO), 0);
	vTaskDelay(pdMS_TO_TICKS(powerKeyDelayEnableMs));
	gpio_set_level(static_cast<gpio_num_t>(CONFIG_SIM800_POWERKEY_GPIO), 1);
	vTaskDelay(pdMS_TO_TICKS(powerKeyDelayDisableMs));

	ESP_LOGI(MODULE, "UART init");
	uart_config_t config = {
		.baud_rate = 57600,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.rx_flow_ctrl_thresh = 122,
		.source_clk = UART_SCLK_APB,
	};

	ESP_LOGI(MODULE, "Driver Init");
	ESP_ERROR_CHECK(uart_driver_install(CONFIG_SIM800_UART_PORT, SIM800_UART_BUFFER_RX, SIM800_UART_BUFFER_TX, 0, NULL, 0));
	ESP_ERROR_CHECK(uart_param_config(CONFIG_SIM800_UART_PORT, &config));

	ESP_LOGI(MODULE, "PINs init");
	ESP_ERROR_CHECK(uart_set_pin(CONFIG_SIM800_UART_PORT, CONFIG_SIM800_TX_GPIO, CONFIG_SIM800_RX_GPIO, UART_PIN_NO_CHANGE,
								 UART_PIN_NO_CHANGE));

	ESP_LOGI(MODULE, "Modem init");
	vTaskDelay(pdMS_TO_TICKS(1000));
	BaseType_t result = xTaskCreate(recvReceiver, "sim800-recv", recvStackSize, nullptr, recvPriority, &recvHandle);
	if (result != pdPASS) {
		ESP_LOGE(MODULE, "Recv Task create error");
		return ESP_FAIL;
	}

	ESP_ERROR_CHECK(consoleAdd("AT", "Send AT-command to modem", &sendCommand));
	return ESP_OK;
} catch (const std::system_error &e) {
	ESP_LOGE(MODULE, "Init esp error %s", e.what());
	return e.code().value();
} catch (const std::exception &e) {
	ESP_LOGE(MODULE, "Init error %s", e.what());
	return ESP_FAIL;
}

esp_err_t simSend(const void *message, size_t length, TickType_t wait) noexcept {
	if (uart_write_bytes(CONFIG_SIM800_UART_PORT, reinterpret_cast<const char *>(message), length) != length) {
		ESP_LOGE(MODULE, "send %zu error", length);
		return ESP_FAIL;
	}

	return (0 == wait)?ESP_OK:uart_wait_tx_done(CONFIG_SIM800_UART_PORT, wait);
}

esp_err_t simSend(const char *message) noexcept {
	return simSend(reinterpret_cast<const void *>(message), strlen(message), 0);
}

int simRecv(uint8_t *data, size_t maxlen) noexcept {
	return -1;
}

