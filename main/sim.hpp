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

#include <freertos/FreeRTOS.h>
#include <esp_err.h>

esp_err_t simInit() noexcept;
esp_err_t simSend(const void *message, size_t length, TickType_t sendTimeout = 0) noexcept;
esp_err_t simSend(const char *message) noexcept;
int simRecv(uint8_t *data, size_t maxlen) noexcept;
