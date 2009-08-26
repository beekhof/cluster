TARGET= group_tool

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o

CFLAGS += -I$(S) -I$(S)/../daemon/ -I$(S)/../lib/
CFLAGS += -I${dlmcontrolincdir}
CFLAGS += -I${fencedincdir}
CFLAGS += -I$(S)/../libgfscontrol
CFLAGS += -I${incdir}
CFLAGS += -I${KERNEL_SRC}/include/

LDFLAGS += -L${dlmcontrollibdir} -ldlmcontrol
LDFLAGS += -L${fencedlibdir} -lfenced
LDFLAGS += -L../lib -lgroup
LDFLAGS += -L../libgfscontrol -lgfscontrol
LDFLAGS += -L${libdir}

LDDEPS += ../lib/libgroup.a
LDDEPS += ../libgfscontrol/libgfscontrol.a

${TARGET}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean
