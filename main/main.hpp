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

#include <esp_err.h>

[[noreturn]] void fatalError(const char *message, const char *tag = nullptr) noexcept;
void fatalError(esp_err_t code, const char *message, const char *tag = nullptr) noexcept;
void throwError(esp_err_t code, const char *message, const char *tag = nullptr);

#define _STRINGIFY(X) #X
#define THROW_ERROR(X) throwError(X, __FILE__ ":" _STRINGIFY(__LINE__))
