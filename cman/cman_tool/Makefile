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
CFLAGS += -I${cmanincdir}
CFLAGS += -I${incdir}
CFLAGS += -I${ccsincdir}

LDFLAGS += -L${cmanlibdir} -lcman
LDFLAGS += -L${libdir}
LDFLAGS += -L${ccslibdir} -lconfdb -lccs

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
