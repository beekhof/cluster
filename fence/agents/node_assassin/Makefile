include ../../../make/defines.mk

MANTARGET	= fence_na.8

FENCEAGENTSLIB	= $(S)/fence_na.lib

all: $(TARGET) $(MANTARGET)

include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

$(MANTARGET): $(S)/fence_na.pod
	pod2man --section=8 $< $@

clean: generalclean
	rm -rf $(MANTARGET)
