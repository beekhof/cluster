install:
ifdef LIBDIRT
	install -d ${libdir}
	install -m644 ${LIBDIRT} ${libdir}
endif
ifdef LIBSYMT
	cp -a ${LIBSYMT} ${libdir}
endif
ifdef INCDIRT
	install -d ${incdir}
	set -e; \
	for i in ${INCDIRT}; do \
		install -m644 $(S)/$$i ${incdir}; \
	done
endif
ifdef FORCESBINT
	mkdir -p ${DESTDIR}/sbin
	install ${FORCESBINT} ${DESTDIR}/sbin
endif
ifdef SBINDIRT
	install -d ${sbindir}
	install -m755 ${SBINDIRT} ${sbindir}
endif
ifdef SBINSYMT
	install -d ${sbindir}
	cp -a ${SBINSYMT} ${sbindir}
endif
ifdef LCRSOT
	install -d ${libexecdir}/lcrso
	install -m644 ${LCRSOT} ${libexecdir}/lcrso
endif
ifdef INITDT
	install -d ${initddir}
	set -e; \
	for i in ${INITDT}; do \
		if [ -f $$i ]; then \
			install -m755 $$i ${initddir}; \
		else \
			install -m755 $(S)/$$i ${initddir}; \
		fi; \
	done
endif
ifdef UDEVT
	install -d ${DESTDIR}/etc/udev/rules.d
	set -e; \
	for i in ${UDEVT}; do \
		install -m644 $(S)/$$i ${DESTDIR}/etc/udev/rules.d; \
	done
endif
ifdef KMODT
	install -d ${module_dir}/${KDIRT}
	install -m644 ${KMODT} ${module_dir}/${KDIRT}
endif
ifdef KHEADT
	install -d ${incdir}/linux
	install -m644 ${KHEADT} ${incdir}/linux
endif
ifdef FENCEAGENTSLIB
	install -d ${DESTDIR}/${fenceagentslibdir}
	install -m644 ${FENCEAGENTSLIB} ${DESTDIR}/${fenceagentslibdir}
endif
ifdef DOCS
	install -d ${docdir}
	set -e; \
	for i in ${DOCS}; do \
		install -m644 $(S)/$$i ${docdir}; \
	done
endif
ifdef LOGRORATED
	install -d ${logrotatedir}
	install -m644 ${LOGRORATED} ${logrotatedir}
endif
ifdef NOTIFYD
	install -d ${notifyddir}
	install -m755 ${NOTIFYD} ${notifyddir}
endif
ifdef PKGCONF
	install -d ${pkgconfigdir}
	install -m644 ${PKGCONF} ${pkgconfigdir}
endif
ifdef SHAREDIRT
	install -d ${sharedir}
	install -m644 ${SHAREDIRT} ${sharedir}
endif
ifdef MANTARGET
	set -e; \
	for i in ${MANTARGET}; do \
		p=`echo $$i | sed -e 's#.*\.##g'`; \
		install -d ${mandir}/man$$p; \
		install -m644 $$i ${mandir}/man$$p; \
	done
endif
