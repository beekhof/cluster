TARGET = fenced

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	config.o \
	cpg.o \
	group.o \
	main.o \
	member_cman.o \
	recover.o \
	logging.o

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${logtincdir} -I${corosyncincdir}
CFLAGS += -I${fenceincdir} -I${fencedincdir}
CFLAGS += -I$(S) -I$(S)/../include -I$(SRCDIR)/group/lib
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
LDFLAGS += -L${logtlibdir} -L${fencelibdir} -llogthread -lfence
LDFLAGS += -L${corosynclibdir} -lcpg -lpthread
LDFLAGS += -L../../group/lib -l group
LDFLAGS += -L${libdir}

LDDEPS += ../../group/lib/libgroup.a

${TARGET}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libfence

clean: generalclean

-include $(OBJS:.o=.d)
