TARGET1= fence_xvmd
TARGET2= testprog

MANTARGET=$(TARGET1).8

SBINDIRT=$(TARGET1)

all: ${TARGET1}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS1=	fence_xvmd.o \
	virt.o \
	options-ccs.o \
	vm_states.o \
	xml.o \
	mcast.o \
	simple_auth.o \
	tcp.o \
	options.o \
	debug.o


OBJS2=	xml-standalone.o

CFLAGS += -D_GNU_SOURCE
CFLAGS += -I${ccsincdir} -I${cmanincdir}
CFLAGS += -I${corosyncincdir} -I${openaisincdir}
CFLAGS += -I${logtincdir}
CFLAGS += -I${virtincdir} -I${nssincdir} -I${nsprincdir}
CFLAGS += `xml2-config --cflags`
CFLAGS += -I${incdir}

STANDALONE_CFLAGS += -DSTANDALONE

LDFLAGS += -L${nsslibdir} -lnss3
LDFLAGS += -L${logtlibdir} -llogthread
LDFLAGS += -L${libdir}
LDFLAGS += -L${ccslibdir} -lccs -L${cmanlibdir} -lcman
LDFLAGS += -L${virtlibdir} -lvirt
LDFLAGS += -L${openaislibdir} -lSaCkpt

XML_LDFLAGS += `xml2-config --libs`

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(XML_LDFLAGS) $(LDFLAGS)

${TARGET2}: ${OBJS2}
	$(CC) -o $@ $^ $(XML_LDFLAGS)

clean: generalclean

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
