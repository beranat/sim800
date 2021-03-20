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
#pragma once

#include <string>
#include <nvs_flash.h>

class Storage {
	Storage(const Storage &&) = delete;
	Storage &operator=(const Storage &&) = delete;

	bool isValid_  = false;
	nvs_handle_t handle_ {};

	Storage() noexcept;
	virtual ~Storage() noexcept;

public:
	constexpr bool operator !() const noexcept {
		return !isValid_;
	}

	int get(const char *name, int def) const noexcept;
	unsigned get(const char *name, unsigned def) const noexcept;
	float get(const char *name, float def) const noexcept;
	std::string get(const char *name, std::string_view def) const noexcept;

	bool set(const char *name, int value) noexcept;
	bool set(const char *name, unsigned value) noexcept;
	bool set(const char *name, float value) noexcept;
	bool set(const char *name, const char *value) noexcept;

	static Storage &getInstance() noexcept;
};

