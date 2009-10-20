TARGET= fence_ipmilan

SBINDIRT=$(TARGET)

MANTARGET=$(TARGET).8

all: ${TARGET} ${MANTARGET}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	expect.o \
	ipmilan.o

CFLAGS += -I${incdir}

LDFLAGS += -L${libdir}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

$(MANTARGET): $(TARGET) ${SRCDIR}/fence/agents/lib/fence2man.xsl
	set -e && \
	./$(TARGET) -o metadata > .$@.tmp && \
	xsltproc ${SRCDIR}/fence/agents/lib/fence2man.xsl .$@.tmp > $@

clean: generalclean
	rm -f $(MANTARGET) .$(MANTARGET).tmp

-include $(OBJS:.o=.d)
