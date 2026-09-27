/* SPDX-License-Identifier: GPL-3.0-or-later
 * Use ChibiOS formatting without linking the flight HAL's stdio/USB console.
 */
#include <hal.h>
#include <chprintf.h>
#include <stdarg.h>
#include <stddef.h>
int __wrap_vsnprintf(char *, size_t, const char *, va_list);
int __wrap_snprintf(char *, size_t, const char *, ...);
void setup_usb_strings(void);
void AP_stack_overflow(const char *);

int __wrap_vsnprintf(char *buffer, size_t size, const char *format, va_list args)
{
    return chvsnprintf(buffer, size, format, args);
}

int __wrap_snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = chvsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

/* Reader descriptors are initialized in main; board early-init needs this hook. */
void setup_usb_strings(void)
{
}

void AP_stack_overflow(const char *thread_name)
{
    (void)thread_name;
    chSysHalt("reader stack overflow");
}