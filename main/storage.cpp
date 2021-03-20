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

#include <atomic>
#include <string>

#include <esp_log.h>
#include <nvs_flash.h>

#include "storage.hpp"
constexpr const char *MODULE = "STORAGE";

namespace {

class Subsystem final {
	static constexpr const char *MODULE = "NVS";
	Subsystem(const Subsystem &) = delete;
	Subsystem &operator=(const Subsystem &) = delete;

	std::atomic<size_t> count_ = 0;	// 0 - not inited, 1 inited, >1 used

	Subsystem() noexcept {
		const esp_err_t init = nvs_flash_init();
		switch (init) {
			case ESP_OK:
				count_.store(1);
				break;
			case ESP_ERR_NVS_NO_FREE_PAGES: {
				ESP_LOGE(MODULE, "No pages available (truncuted?)");
				const esp_err_t erase = nvs_flash_erase();
				if (ESP_OK != erase)
					ESP_LOGE(MODULE, "Erase error %s(%d)", esp_err_to_name(init), static_cast<int>(init));
				else
					count_.store(1);
			}
			break;
			default:
				ESP_LOGE(MODULE, "Subsystem error %s(%d)", esp_err_to_name(init), static_cast<int>(init));
				break;
		}
	}

	~Subsystem() noexcept {
		const size_t count = count_.load();
		if (1 < count) {
			ESP_LOGE(MODULE, "NVS-flash dector refs (%zu)", count);
			assert(false);
		}

		if (0 != count) {
			const esp_err_t deinit = nvs_flash_deinit();
			if (ESP_OK != deinit) {
				ESP_LOGE(MODULE, "Deinit error %s(%d)", esp_err_to_name(deinit), static_cast<int>(deinit));
				assert(false);
			}

			count_.store(0);
		}
	}

public:
	static Subsystem &getInstance() noexcept;

	bool inc() noexcept {
		while (true) {
			const size_t count = count_.load(std::memory_order_acquire);
			if (0 == count) {
				ESP_LOGE(MODULE, "NVS-flash non-init error");
				return false;
			}

			size_t desired = count;
			if (count_.compare_exchange_weak(desired, count+1))
				return true;
		}
	}

	bool dec() noexcept {
		while (true) {
			const size_t count = count_.load(std::memory_order_acquire);
			if (0 == count)
				return false;

			if (1 == count) {
				ESP_LOGE(MODULE, "NVS-flash -- overrefs refs");
				assert(false);
				return true;
			}

			size_t desired = count;
			if (count_.compare_exchange_weak(desired, count-1))
				return true;
		}
	}

	bool operator--() noexcept {
		return dec();
	}

	bool operator++() noexcept {
		return inc();
	}
};

Subsystem &Subsystem::getInstance() noexcept {
	static Subsystem instance;
	return instance;
}

} // namespace

template <class T> T get(nvs_handle_t handle, const char *name, T def, esp_err_t (*fn)(nvs_handle_t, const char *,
						 T *)) noexcept {
	T value;
	const esp_err_t result = fn(handle, name, &value);
	if (ESP_OK == result) {
		ESP_LOGI(MODULE, "Storage get %s", name);
		return value;
	}

	ESP_LOGI(MODULE, "Storage get %s - %d", name, static_cast<int>(result));
	if (ESP_ERR_NVS_NOT_FOUND == result)
		return def;

	ESP_LOGW(MODULE, "Storage get `%s' error %s (%d)", name,  esp_err_to_name(result), static_cast<int>(result));
	return def;
}

template <class T> bool set(nvs_handle_t handle, const char *name, const T &value, esp_err_t (*fn)(nvs_handle_t,
							const char *, T)) noexcept {
	ESP_LOGI(MODULE, "Save %s", name);

	const esp_err_t result = fn(handle, name, value);
	if (ESP_OK == result)
		return true;

	ESP_LOGW(MODULE, "Storage set `%s' error %s (%d)", name,  esp_err_to_name(result), static_cast<int>(result));
	return false;
}

Storage &Storage::getInstance() noexcept {
	static Storage instance;
	return instance;
}

Storage::Storage() noexcept {
	if (!++Subsystem::getInstance())
		return;

	nvs_handle_t handle;
	const esp_err_t open = nvs_open(MODULE, NVS_READWRITE, &handle);
	if (ESP_OK == open) {
		isValid_ = true;
		handle_ = handle;
	} else {
		--Subsystem::getInstance();
		ESP_LOGE(MODULE, "Open error %s (%d)", esp_err_to_name(open), static_cast<int>(open));
		isValid_ = false;
	}
}

Storage::~Storage() noexcept {
	if (isValid_) {
		nvs_close(handle_);
		isValid_ = false;
		--Subsystem::getInstance();
	}
}

int Storage::get(const char *name, int def) const noexcept {
	static_assert(sizeof(def) == sizeof(int32_t));
	if (!*this) {
		ESP_LOGW(MODULE, "Storage not inited %s use default value", name);
		return def;
	}
	return ::get(handle_, name, def, &nvs_get_i32);
}


unsigned Storage::get(const char *name, unsigned def) const noexcept {
	static_assert(sizeof(def) == sizeof(uint32_t));
	if (!*this) {
		ESP_LOGW(MODULE, "Storage not inited %s use default value", name);
		return def;
	}
	return ::get(handle_, name, def, &nvs_get_u32);
}

float Storage::get(const char *name, float def) const noexcept {
	static_assert(sizeof(def) == sizeof(uint32_t));
	if (!*this) {
		ESP_LOGW(MODULE, "Storage not inited %s use default value", name);
		return def;
	}

	std::string fname(name);
	fname.append("-float");

	uint32_t value;
	const esp_err_t result = nvs_get_u32(handle_, fname.c_str(), &value);
	if (ESP_OK == result) {
		float v;
		memcpy(&v, &value, sizeof(value));
		return v;
	}

	if (ESP_ERR_NVS_NOT_FOUND != result)
		ESP_LOGE(MODULE, "Storage %s get error %s (%d)", name,  esp_err_to_name(result), static_cast<int>(result));

	return def;
}

std::string Storage::get(const char *name, std::string_view def) const noexcept {
	constexpr size_t defaultLength = 64;

	if (!*this) {
		ESP_LOGW(MODULE, "Storage not inited %s use default value", name);
		return std::string(def);
	}
	std::string value(defaultLength, '?');

	for (bool isCompleted = false; !isCompleted;) {
		isCompleted = true;
		size_t length = value.length();
		const esp_err_t result = nvs_get_str(handle_, name, value.data(), &length);
		switch (result) {
			case ESP_OK:
				value.resize(length-1);
				break;
			case ESP_ERR_NVS_INVALID_LENGTH:
				isCompleted = false;
				value.resize(length);
				break;
			default:
				ESP_LOGE(MODULE, "Storage %s get error %s (%d)", name,  esp_err_to_name(result), static_cast<int>(result));
				[[fallthrough]];
			case ESP_ERR_NVS_NOT_FOUND:
				value = def;
				break;
		}
	};

	return value;
}

bool Storage::set(const char *name, int value) noexcept {
	ESP_LOGI(MODULE, "Save int %s", name);
	static_assert(sizeof(value) == sizeof(int32_t));
	if (!*this) {
		ESP_LOGW(MODULE, "Storage not inited %s use default value", name);
		return false;
	}
	return ::set(handle_, name, value, &nvs_set_i32);
}

bool Storage::set(const char *name, unsigned value) noexcept {
	ESP_LOGI(MODULE, "Save uint %s", name);
	static_assert(sizeof(value) == sizeof(uint32_t));
	if (!*this) {
		ESP_LOGW(MODULE, "Storage not inited %s use default value", name);
		return false;
	}
	return ::set(handle_, name, value, &nvs_set_u32);
}

bool Storage::set(const char *name, float value) noexcept {
	static_assert(sizeof(value) == sizeof(uint32_t));
	if (!*this) {
		ESP_LOGW(MODULE, "Storage not inited %s use default value", name);
		return false;
	}

	std::string fname(name);
	fname.append("-float");
	uint32_t v;
	memcpy(&v, &value, sizeof(v));
	return ::set(handle_, fname.c_str(), v, &nvs_set_u32);
}

bool Storage::set(const char *name, const char *value) noexcept {
	if (!*this) {
		ESP_LOGW(MODULE, "Storage not inited %s use default value", name);
		return false;
	}
	return ::set<const char *>(handle_, name, value, &nvs_set_str);
}
