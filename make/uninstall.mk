uninstall:
ifdef LIBDIRT
	${UNINSTALL} ${LIBDIRT} ${libdir}
endif
ifdef LIBSYMT
	${UNINSTALL} ${LIBSYMT} ${libdir}
endif
ifdef INCDIRT
	${UNINSTALL} ${INCDIRT} ${incdir}
endif
ifdef FORCESBINT
	${UNINSTALL} ${FORCESBINT} ${DESTDIR}/sbin
endif
ifdef SBINDIRT
	${UNINSTALL} ${SBINDIRT} ${sbindir}
endif
ifdef SBINSYMT
	${UNINSTALL} ${SBINSYMT} ${sbindir}
endif
ifdef LCRSOT
	${UNINSTALL} ${LCRSOT} ${libexecdir}/lcrso
endif
ifdef INITDT
	${UNINSTALL} ${INITDT} ${initddir}
endif
ifdef UDEVT
	${UNINSTALL} ${UDEVT} ${DESTDIR}/etc/udev/rules.d
endif
ifdef KMODT
	${UNINSTALL} ${KMODT} ${module_dir}/${KDIRT}
endif
ifdef KHEADT
	${UNINSTALL} ${KHEADT} ${incdir}/linux
endif
ifdef FENCEAGENTSLIB
	${UNINSTALL} ${FENCEAGENTSLIB}* ${DESTDIR}/${fenceagentslibdir}
endif
ifdef DOCS
	${UNINSTALL} ${DOCS} ${docdir}
endif
ifdef LOGRORATED
	${UNINSTALL} ${LOGRORATED} ${logrotatedir}
endif
ifdef NOTIFYD
	${UNINSTALL} ${NOTIFYD} ${notifyddir}
endif
ifdef PKGCONF
	${UNINSTALL} ${PKGCONF} ${pkgconfigdir}
endif
ifdef SHAREDIRT
	${UNINSTALL} ${SHAREDIRT} ${sharedir}
endif
ifdef MANTARGET
	set -e; \
	for i in ${MANTARGET}; do \
		p=`echo $$i | sed -e 's#.*\.##g'`; \
		${UNINSTALL} $$i ${mandir}/man$$p; \
	done
endif
ifdef CONFFILEEXAMPLE
	${UNINSTALL} ${CONFFILEEXAMPLE} ${DESTDIR}/${CONFDIR}
endif
