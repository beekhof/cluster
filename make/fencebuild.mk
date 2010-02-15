ifndef FENCEAGENTSLIB
	ifndef SBINDIRT
		SBINDIRT=$(TARGET)
	endif
	ifndef MANTARGET
		MANTARGET=$(TARGET).8
	endif
endif

all: $(TARGET) $(MANTARGET)

include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

$(TARGET):
	${FENCEPARSE} \
		${SRCDIR}/make/copyright.cf REDHAT_COPYRIGHT \
		${RELEASE_VERSION} \
		$(S) $@ | \
	sed \
		-e 's#@FENCEAGENTSLIBDIR@#${fenceagentslibdir}#g' \
		-e 's#@SNMPBIN@#${snmpbin}#g' \
		-e 's#@LOGDIR@#${logdir}#g' \
		-e 's#@SBINDIR@#${sbindir}#g' \
	> $@

ifdef MAKEMAN
$(MANTARGET): $(MANTARGET:.8=) ${SRCDIR}/fence/agents/lib/fence2man.xsl
	set -e && \
	PYTHONPATH=${OBJDIR}/fence/agents/lib \
		python $^ -o metadata > .$@.tmp && \
	xsltproc ${SRCDIR}/fence/agents/lib/fence2man.xsl .$@.tmp > $@

clean: generalclean
	rm -f $(MANTARGET) .$(MANTARGET).tmp
else
$(MANTARGET): $(S)/$(MANTARGET)
	cp $< $@

clean: generalclean
	if [ "$(OBJDIR)" != "$(SRCDIR)" ]; then rm -f $(MANTARGET); fi
endif
