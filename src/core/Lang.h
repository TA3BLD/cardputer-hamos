// Interface language. Code is written with English text wrapped in T("...");
// with the "lang" setting = tr, T() returns the Turkish translation from
// Lang.cpp (English when there is none yet).
#pragma once
#include <string>

namespace lang {
bool turkish();
void reload();                              // after the setting changed
const char* tr(const char* english);
}

#define T(s) lang::tr(s)
