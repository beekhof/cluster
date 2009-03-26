TARGET=cman

INITDT=$(TARGET)

all: $(TARGET)

include ../../make/defines.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

$(TARGET): $(S)/$(TARGET).in
	cat $(S)/$(TARGET).in | sed \
		-e 's#@SBINDIR@#${sbindir}#g' \
		-e 's#@INITDDIR@#${initddir}#g' \
		-e 's#@NOTIFYDDIR@#${notifyddir}#g' \
		-e 's#@CONFDIR@#${CONFDIR}#g' \
		-e 's#@CONFFILE@#${CONFFILE}#g' \
	> $(TARGET)

clean: generalclean
