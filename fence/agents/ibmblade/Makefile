include ../../../make/defines.mk

TARGET = fence_ibmblade
MANTARGET = $(TARGET).8
SBINSYMT = $(TARGET)

all: $(TARGET)

include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk
include $(OBJDIR)/make/clean.mk

$(TARGET):
	ln -sf fence_bladecenter_snmp $@

clean: generalclean
