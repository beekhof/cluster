TARGET = fence_node

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	fence_node.o

CFLAGS += -D_FILE_OFFSET_BITS=64

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${logtincdir}
CFLAGS += -I${fenceincdir} -I${fencedincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
LDFLAGS += -L${fencelibdir} -L${fencedlibdir} -lfence -lfenced
LDFLAGS += -L${logtlibdir} -llogthread
LDFLAGS += -L${libdir}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libfence
	$(MAKE) -C ../libfenced

clean: generalclean

-include $(OBJS:.o=.d)
