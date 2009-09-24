include ../../make/defines.mk

TARGET= gfs_controld
ifdef enable_pacemaker
TARGET += gfs_controld.pcmk
SBINDIRT += gfs_controld.pcmk
endif

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

SHAREDOBJS= 	main.o \
	crc.o \
	config.o \
	cpg-new.o \
	cpg-old.o \
	group.o \
	util.o \
	plock.o \
	logging.o


PCMK_OBJS=	member_pcmk-pcmk.o \
		$(SHAREDOBJS:.o=-pcmk.o)

OBJS=		$(SHAREDOBJS) \
		member_cman.o

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${logtincdir} -I${dlmcontrolincdir}
CFLAGS += -I${corosyncincdir} -I${openaisincdir} -I${fencedincdir}
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../libgfscontrol
CFLAGS += -I$(S)/../lib/ -I$(S)/../include/
CFLAGS += -I${incdir}

PCMK_CFLAGS += -DENABLE_PACEMAKER=1
PCMK_CFLAGS += -I${incdir}/heartbeat -I${incdir}/pacemaker
PCMK_CFLAGS += `pkg-config glib-2.0 --cflags`
PCMK_CFLAGS += `xml2-config --cflags`

LDFLAGS += -L${dlmcontrollibdir} -ldlmcontrol
LDFLAGS += -L${logtlibdir} -llogthread
LDFLAGS += -L${corosynclibdir} -lcpg -lpthread
LDFLAGS += -L${openaislibdir} -lSaCkpt
LDFLAGS += -L../lib -lgroup
LDFLAGS += -L${libdir}

PCMK_LDFLAGS += -L${ccslibdir} -lccs
PCMK_LDFLAGS += -lcib -lcrmcommon -lcrmcluster -ltotem_pg
PCMK_LDFLAGS += `pkg-config glib-2.0 --libs`
PCMK_LDFLAGS += `xml2-config --libs`

CMAN_LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
CMAN_LDFLAGS += -L${fencedlibdir} -lfenced

LDDEPS += ../lib/libgroup.a

gfs_controld: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS) $(CMAN_LDFLAGS)

gfs_controld.pcmk: ${PCMK_OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS) $(PCMK_LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
