# Keep this first line.
GOSSAMER_PATH=gossamer

# End of user configurable options.

# Support USB features?
TINYUSB_CDC=0

# Now we're all set to include gossamer's make rules.
include $(GOSSAMER_PATH)/make.mk

# Don't add gossamer's rtc.c since we are using our own rtc32.c
SRCS := $(filter-out $(GOSSAMER_PATH)/peripherals/rtc.c,$(SRCS))

CFLAGS+=-D_POSIX_C_SOURCE=200112L

define n


endef

# Don't require BOARD or DISPLAY for `make clean` or `make install`
ifeq (,$(filter clean,$(MAKECMDGOALS)))
  ifeq (,$(filter install,$(MAKECMDGOALS)))
    ifndef BOARD
      $(error Build failed: BOARD not defined. Use one of the four options below, depending on your hardware:$n$n    make BOARD=sensorwatch_red DISPLAY=display_type$n    make BOARD=sensorwatch_blue DISPLAY=display_type$n    make BOARD=sensorwatch_pro DISPLAY=display_type$n$n)
    endif
  endif

  ifeq (,$(filter install,$(MAKECMDGOALS)))
    ifndef DISPLAY
      $(error Build failed: DISPLAY not defined. Use one of the options below, depending on your hardware:$n$n    make BOARD=board_type DISPLAY=classic$n    make BOARD=board_type DISPLAY=custom$n$n)
    else
      ifeq ($(DISPLAY), custom)
        DEFINES += -DFORCE_CUSTOM_LCD_TYPE
      else ifeq ($(DISPLAY), classic)
        DEFINES += -DFORCE_CLASSIC_LCD_TYPE
      else ifeq ($(DISPLAY), autodetect)
        $(warning WARNING: LCD autodetection is experimental and not reliable! We suggest specifying DISPLAY=classic or DISPLAY=custom for reliable operation.)
      else
        $(error Build failed: invalid DISPLAY type. Use one of the options below, depending on your hardware:$n$n    make BOARD=board_type DISPLAY=classic$n    make BOARD=board_type DISPLAY=custom$n$n)
      endif
    endif
  endif
endif

# Add your include directories here.
INCLUDES += \
  -I./ \
  -I. \
  -I./utz \
  -I./watch-library/shared/watch \
  -I./watch-library/shared/driver \
  -I$(GOSSAMER_PATH)/drivers/tinyusb/src/ \

# Add your source files here.
SRCS += \
  ./dummy.c \
  ./utz/utz.c \
  ./utz/zones.c \
  ./watch-library/shared/watch/watch_common_buzzer.c \
  ./watch-library/shared/watch/watch_common_display.c \
  ./watch-library/shared/watch/watch_utility.c \

ifdef EMSCRIPTEN

INCLUDES += \
  -I./watch-library/simulator/watch \

SRCS += \
  ./watch-library/simulator/watch/watch.c \
  ./watch-library/simulator/watch/watch_adc.c \
  ./watch-library/simulator/watch/watch_deepsleep.c \
  ./watch-library/simulator/watch/watch_extint.c \
  ./watch-library/simulator/watch/watch_gpio.c \
  ./watch-library/simulator/watch/watch_i2c.c \
  ./watch-library/simulator/watch/watch_private.c \
  ./watch-library/simulator/watch/watch_rtc.c \
  ./watch-library/simulator/watch/watch_slcd.c \
  ./watch-library/simulator/watch/watch_spi.c \
  ./watch-library/simulator/watch/watch_storage.c \
  ./watch-library/simulator/watch/watch_tcc.c \
  ./watch-library/simulator/watch/watch_uart.c \

else

INCLUDES += \
  -I./watch-library/hardware/watch \

SRCS += \
  ./watch-library/hardware/watch/rtc32.c \
  ./watch-library/hardware/watch/watch.c \
  ./watch-library/hardware/watch/watch_adc.c \
  ./watch-library/hardware/watch/watch_deepsleep.c \
  ./watch-library/hardware/watch/watch_extint.c \
  ./watch-library/hardware/watch/watch_gpio.c \
  ./watch-library/hardware/watch/watch_i2c.c \
  ./watch-library/hardware/watch/watch_private.c \
  ./watch-library/hardware/watch/watch_rtc.c \
  ./watch-library/hardware/watch/watch_slcd.c \
  ./watch-library/hardware/watch/watch_spi.c \
  ./watch-library/hardware/watch/watch_storage.c \
  ./watch-library/hardware/watch/watch_tcc.c \
  ./watch-library/hardware/watch/watch_uart.c \

endif

SRCS += \
  ./app.c \

# Finally, leave this line at the bottom of the file.
include $(GOSSAMER_PATH)/rules.mk
