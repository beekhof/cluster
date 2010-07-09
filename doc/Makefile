DOCS =	usage.txt \
	COPYING.applications \
	COPYING.libraries \
	COPYRIGHT \
	README.licence \
	cluster_conf.html

TARGET= cluster

LOGRORATED = $(TARGET)

all: $(TARGET)

include ../make/defines.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk
include $(OBJDIR)/make/clean.mk

$(TARGET):
	cat $(S)/$(TARGET).logrotate.in | sed \
		-e 's#@LOGDIR@#${logdir}#g' \
	> $(TARGET)

clean: generalclean
