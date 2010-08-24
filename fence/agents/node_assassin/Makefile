include ../../../make/defines.mk

TARGET		= fence_na

MANTARGET	= $(TARGET).8

FENCEAGENTSLIB	= $(TARGET).lib

NACONFFILE	= $(TARGET).conf

PODFILE		= $(TARGET).pod

EXTRACONFFILE	= $(S)/$(NACONFFILE)

all: $(TARGET) $(PODFILE) $(MANTARGET) $(FENCEAGENTSLIB)

include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

$(PODFILE): $(S)/$(PODFILE).in
	cat $^ | sed \
		-e 's#@NACONFFILE@#${CONFDIR}/$(NACONFFILE)#g' \
	> $@

$(MANTARGET): $(PODFILE)
	pod2man --section=8 $< $@

$(FENCEAGENTSLIB): $(S)/$(FENCEAGENTSLIB).in
	cat $^ | sed \
		-e 's#@NACONFFILE@#${CONFDIR}/$(NACONFFILE)#g' \
	> $@

$(TARGET): $(S)/$(TARGET).pl
	cat $^ | sed \
		-e 's#@NACONFFILE@#${CONFDIR}/$(NACONFFILE)#g' \
	> $@

clean: generalclean
	rm -rf $(MANTARGET) $(FENCEAGENTSLIB) $(PODFILE)
