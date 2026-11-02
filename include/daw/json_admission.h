#pragma once
#include <stdbool.h>
#include <stddef.h>
// Validates bounded JSON grammar and unique keys before typed artifact decoding.
bool daw_json_structure_valid(const char* data,size_t size,const char* required_root_key);
