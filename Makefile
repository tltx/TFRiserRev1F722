# Makefile for TFRiserRev1F722 — mirrors STM32CubeIDE Debug build settings.
# Targets STM32F722RETx (Cortex-M7, FPv5-SP-D16 hard-float).

TARGET   := TFRiserRev1F722
BUILD    ?= build
PREFIX   := arm-none-eabi-
CC       := $(PREFIX)gcc
AS       := $(PREFIX)gcc -x assembler-with-cpp
CP       := $(PREFIX)objcopy
SZ       := $(PREFIX)size

# CPU / FPU
CPU      := -mcpu=cortex-m7
FPU      := -mfpu=fpv5-sp-d16
FLOAT    := -mfloat-abi=hard
MCU      := $(CPU) -mthumb $(FPU) $(FLOAT)

# Defines (from .cproject Debug config)
C_DEFS := \
  -DUSE_HAL_DRIVER \
  -DSTM32F722xx \
  -DDEBUG

# Includes (from .cproject Debug config)
C_INCLUDES := \
  -IUSB_HOST/App \
  -IDrivers/CMSIS/Include \
  -IDrivers/CMSIS/Device/ST/STM32F7xx/Include \
  -IMiddlewares/ST/STM32_USB_Host_Library/Class/HID/Inc \
  -ICore/Inc \
  -IDrivers/STM32F7xx_HAL_Driver/Inc \
  -IUSB_HOST/Target \
  -IDrivers/STM32F7xx_HAL_Driver/Inc/Legacy \
  -IMiddlewares/ST/STM32_USB_Host_Library/Core/Inc

# Sources
C_SOURCES := \
  Core/Src/main.c \
  Core/Src/amiga.c \
  Core/Src/rtc_msm6242.c \
  Core/Src/stm32f7xx_hal_msp.c \
  Core/Src/dwt_delay.c \
  Core/Src/system_stm32f7xx.c \
  Core/Src/stm32f7xx_it.c \
  Core/Src/utils.c \
  Core/Src/syscalls.c \
  Core/Src/sysmem.c \
  Core/Src/gamepad_map.c \
  USB_HOST/App/usb_host.c \
  USB_HOST/Target/usbh_conf.c \
  Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_pipes.c \
  Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_ctlreq.c \
  Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_core.c \
  Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_ioreq.c \
  Middlewares/ST/STM32_USB_Host_Library/Class/HID/Src/usbh_hid_mouse.c \
  Middlewares/ST/STM32_USB_Host_Library/Class/HID/Src/usbh_hid_keybd.c \
  Middlewares/ST/STM32_USB_Host_Library/Class/HID/Src/usbh_hid_parser.c \
  Middlewares/ST/STM32_USB_Host_Library/Class/HID/Src/usbh_hid.c \
  Middlewares/ST/STM32_USB_Host_Library/Class/HID/Src/usbh_hid_reportparser.c \
  Middlewares/ST/STM32_USB_Host_Library/Class/HID/Src/usbh_hid_gamepad.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma_ex.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_i2c_ex.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_hcd.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr_ex.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_gpio.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_exti.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_ll_usb.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_tim_ex.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_cortex.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_tim.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rtc_ex.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_i2c.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rtc.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash_ex.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc_ex.c \
  Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash.c

ASM_SOURCES := Core/Startup/startup_stm32f722retx.s

# Build flags (Debug: -O0 -g3)
OPT      ?= -O0
CFLAGS   := $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections -fcommon -g3 -gdwarf-2 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@"
ASFLAGS  := $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections -g3 -gdwarf-2 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@"

# Linker
LDSCRIPT := STM32F722RETX_FLASH.ld
LIBS     := -lc -lm -lnosys
LDFLAGS  := $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--gc-sections

# Object lists
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD)/,$(notdir $(ASM_SOURCES:.s=.o)))

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(CP) -O ihex $< $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(CP) -O binary -S $< $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

# --- Flashing -------------------------------------------------------------
# STM32F722 flash starts at 0x08000000.

# ST-LINK via stlink-tools
flash-stlink: $(BUILD)/$(TARGET).bin
	st-flash --reset write $< 0x08000000

# OpenOCD (works with ST-LINK, CMSIS-DAP, J-Link, etc.)
OPENOCD       ?= openocd
OPENOCD_IFACE ?= interface/stlink.cfg
OPENOCD_TGT   ?= target/stm32f7x.cfg
flash-openocd: $(BUILD)/$(TARGET).elf
	$(OPENOCD) -f $(OPENOCD_IFACE) -f $(OPENOCD_TGT) \
	  -c "program $< verify reset exit"

# DFU (built-in USB bootloader; pull BOOT0 high and reset before running)
# STM32F7 DFU VID:PID is 0483:df11, alt setting 0 is internal flash.
flash-dfu: $(BUILD)/$(TARGET).bin
	dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D $<

# Convenience alias — defaults to ST-LINK
flash: flash-stlink

-include $(wildcard $(BUILD)/*.d)

.PHONY: all clean flash flash-stlink flash-openocd flash-dfu
