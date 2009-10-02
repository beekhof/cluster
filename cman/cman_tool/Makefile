TARGET= cman_tool

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o \
	join.o

CFLAGS += -DCOROSYNCBIN=\"${corosyncbin}\" -DSBINDIR=\"${sbindir}\"
CFLAGS += -I${cmanincdir} -I${ccsincdir}
CFLAGS += -I${corosyncincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman -L${ccslibdir} -lccs
LDFLAGS += -L${corosynclibdir} -lconfdb
LDFLAGS += -L${libdir}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
