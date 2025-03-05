KMOD=	framework
SRCS=	\
	acpi_if.h \
	acpi_bus_if.h \
	opt_acpi.h \
	opt_kbd.h \
	backlight_if.h \
	bus_if.h \
	device_if.h \
	framework_evdev_thread.c \
	framework_utils.c \
	framework_state.c \
	framework_evdev.c \
	framework_backlight.c \
	framework_sysctl.c \
	framework_power.c \
	framework_screen.c \
	framework_callout.c \
	framework.c

etags:
	/bin/rm -f TAGS
	find . -name '*.c' -print -or -name '*.h' -print | xargs etags --append

.include <bsd.kmod.mk>
