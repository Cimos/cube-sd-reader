#pragma once
#define be32_to_cpu(x) __builtin_bswap32(x)
#define be16_to_cpu(x) __builtin_bswap16(x)
#define cpu_to_be32(x) __builtin_bswap32(x)
