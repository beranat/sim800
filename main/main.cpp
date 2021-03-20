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
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <string>
#include <exception>
#include <system_error>

#include <esp_log.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <driver/i2c.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "sim.hpp"
#include "console.hpp"
#include "sdkconfig.h"

#include "storage.hpp"
#include "main.hpp"

constexpr const char *APP = "app";

void fatalError(const char *message, const char *tag) noexcept {
	if (nullptr == message || 0 == *message)
		message = "Internal";

	ESP_LOGE((nullptr != tag)?tag:APP, "%s", message);

	while (true)
		esp_deep_sleep_start();
}

void fatalError(esp_err_t code, const char *message, const char *tag) noexcept {
	assert(nullptr != message && 0 != *message);

	if (ESP_OK != code) {
		if (nullptr == message || 0 == *message)
			message = "Internal";

		ESP_LOGE((nullptr != tag)?tag:APP, "%s error %s (%d)", message, esp_err_to_name(code), static_cast<int>(code));
		while (true)
			esp_deep_sleep_start();
	}
}

void throwError(esp_err_t code, const char *message, const char *tag) {
	assert(nullptr != message && 0 != *message);

	if (ESP_OK != code) {
		if (nullptr == message || 0 == *message)
			message = "Internal";

		const int len = snprintf(nullptr, 0, "%s error %s (%d)", message, esp_err_to_name(code), static_cast<int>(code));
		std::string s(len, 0);
		snprintf(s.data(), s.length(), "%s error %s (%d)", message, esp_err_to_name(code), static_cast<int>(code));

		ESP_LOGE((nullptr != tag)?tag:APP, "%s", s.c_str());
		throw std::system_error(code, std::system_category(), std::move(s));
	}
}

void led(bool enable) noexcept {
	if constexpr(-1 != CONFIG_LED_GPIO)
		gpio_set_level(static_cast<gpio_num_t>(CONFIG_LED_GPIO), enable?1:0);
	else
		ESP_LOGI(APP, "LED %s", enable?"ON":"off");
}

constexpr unsigned int CONFIG_IP5306_I2C_FREQ_HZ = 100000;
constexpr unsigned int CONFIG_IP5306_I2C_PORT = 1;
constexpr unsigned int CONFIG_IP5306_I2C_SDA_GPIO = 21;
constexpr unsigned int CONFIG_IP5306_I2C_SCL_GPIO = 22;
constexpr unsigned int CONFIG_IP5306_I2C_ADDR = 0x75;
constexpr unsigned int IP5306_REG_SYS_CTL0 =  0x00;

// setPowerBoostKeepOn
esp_err_t setPowerBoostKeepOn(bool boost) noexcept {

	// Set bit1: Boost Keep On: 1 - enable, 0 - disable(default)
	const uint8_t code = (boost)?0x37:0x35;

	i2c_config_t conf = {
		I2C_MODE_MASTER,
		CONFIG_IP5306_I2C_SDA_GPIO,
		CONFIG_IP5306_I2C_SCL_GPIO,
		GPIO_PULLUP_ENABLE,
		GPIO_PULLUP_ENABLE,
		CONFIG_IP5306_I2C_FREQ_HZ

	};

	fatalError(i2c_param_config(CONFIG_IP5306_I2C_PORT, &conf), "i2c_param_config");
	fatalError(i2c_driver_install(CONFIG_IP5306_I2C_PORT, conf.mode, 0, 0, 0), "i2c_driver_install");


	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	if (nullptr == cmd) {
		ESP_LOGE(APP, "I2C command link create error");
		return ESP_ERR_NO_MEM;
	}

	esp_err_t ret = ESP_FAIL;
	try {
		THROW_ERROR(i2c_master_start(cmd));
		THROW_ERROR(i2c_master_write_byte(cmd, (CONFIG_IP5306_I2C_ADDR << 1) | I2C_MASTER_WRITE, 1));
		THROW_ERROR(i2c_master_write_byte(cmd, IP5306_REG_SYS_CTL0, 1));
		THROW_ERROR(i2c_master_write_byte(cmd, code, 1));
		THROW_ERROR(i2c_master_stop(cmd));
		ret = i2c_master_cmd_begin(CONFIG_IP5306_I2C_PORT, cmd, 1000 / portTICK_RATE_MS);
	} catch (const std::system_error &e) {
		ESP_LOGE(APP, "I2C error %s", e.what());
		ret = e.code().value();
	} catch (const std::exception &e) {
		ESP_LOGE(APP, "I2C exception %s", e.what());
		ret = ESP_FAIL;
	}
	i2c_cmd_link_delete(cmd);
	return ret;
}

int pinDisable(int argc, char *argv[]) {
	if (2 != argc)
		return ESP_ERR_INVALID_ARG;

	size_t pos = 0;
	const int pin = std::stoi(argv[1], &pos);
	if (pos != strlen(argv[1])) {
		ESP_LOGE(APP, "PIN `%s' must be number", argv[1]);
		return ESP_ERR_INVALID_ARG;
	}

	const gpio_config_t config = {
		.pin_bit_mask = BIT64(pin),
		.mode = GPIO_MODE_DISABLE,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};
	return gpio_config(&config);
}

int pinInput(int argc, char *argv[]) {
	if (2 != argc)
		return ESP_ERR_INVALID_ARG;

	size_t pos = 0;
	const int pin = std::stoi(argv[1], &pos);
	if (pos != strlen(argv[1])) {
		ESP_LOGE(APP, "PIN `%s' must be a number", argv[1]);
		return ESP_ERR_INVALID_ARG;
	}

	const gpio_config_t config = {
		.pin_bit_mask = BIT64(pin),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};
	const esp_err_t conf = gpio_config(&config);
	if (ESP_OK != conf)
		return conf;

	const int isEnabled = gpio_get_level(static_cast<gpio_num_t>(pin));
	printf("GPIO #%u = %s", pin, isEnabled?"1-HIGH":"0-low");
	return ESP_OK;
}

int pinOutput(int argc, char *argv[]) {
	// output 12 1/0
	if (3 != argc)
		return ESP_ERR_INVALID_ARG;

	size_t pos = 0;
	const int pin = std::stoi(argv[1], &pos);
	if (pos != strlen(argv[1])) {
		ESP_LOGE(APP, "PIN `%s' must be a number", argv[1]);
		return ESP_ERR_INVALID_ARG;
	}

	const int enable = std::stoi(argv[2], &pos);
	if (pos != strlen(argv[1])) {
		ESP_LOGE(APP, "VALUE `%s' must be a number", argv[1]);
		return ESP_ERR_INVALID_ARG;
	}

	const gpio_config_t config = {
		.pin_bit_mask = BIT64(pin),
		.mode = GPIO_MODE_OUTPUT, // GPIO_MODE_DISABLE, // GPIO_MODE_INPUT, GPIO_MODE_OUTPUT
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};
	const esp_err_t conf = gpio_config(&config);
	if (ESP_OK != conf)
		return conf;

	gpio_set_level(static_cast<gpio_num_t>(pin), enable?1:0);
	return ESP_OK;
}

extern "C" void app_main(void) {
	esp_log_level_set(APP, ESP_LOG_INFO);
	ESP_LOGI(APP, "Initialization");

	if constexpr(-1 != CONFIG_LED_GPIO) {
		gpio_pad_select_gpio(static_cast<gpio_num_t>(CONFIG_LED_GPIO));
		gpio_set_direction(static_cast<gpio_num_t>(CONFIG_LED_GPIO), GPIO_MODE_OUTPUT);

		for (unsigned int i = 0; i < 6; ++i) {
			if (i != 0)
				vTaskDelay(pdMS_TO_TICKS(250));
			led(0 == (i & 1));
		}
	} else
		ESP_LOGW(APP, "LED not available");

	ESP_ERROR_CHECK(consoleInit());

	ESP_ERROR_CHECK(consoleAdd("reboot", "Software reset of the chip", [](int, char **) -> int { esp_restart(); return ESP_FAIL; }));
	ESP_ERROR_CHECK(consoleAdd("pinout", "Configure Pin as Output", &pinOutput));
	ESP_ERROR_CHECK(consoleAdd("pinin", "Configure pin as Input", &pinInput));
	ESP_ERROR_CHECK(consoleAdd("pinoff", "Deconfigure pin", &pinDisable));

	ESP_LOGI(APP, "IP5306 init");
	ESP_ERROR_CHECK(setPowerBoostKeepOn(true));

	// SIM800 configuration
	ESP_ERROR_CHECK(simInit());

	consoleLoop();
	fatalError("System halted", APP);
}
