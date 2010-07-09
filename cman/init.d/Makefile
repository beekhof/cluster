TARGET=cman cman.init.defaults

INITDT=cman 

all: $(TARGET)

include ../../make/defines.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

%: $(S)/%.in
	cat $^ | sed \
		-e 's#@SBINDIR@#${sbindir}#g' \
		-e 's#@INITDDIR@#${initddir}#g' \
		-e 's#@NOTIFYDDIR@#${notifyddir}#g' \
		-e 's#@CONFDIR@#${CONFDIR}#g' \
		-e 's#@CONFFILE@#${CONFFILE}#g' \
	> $@

clean: generalclean
