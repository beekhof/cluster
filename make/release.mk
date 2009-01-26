# NOTE: this make file snippet is only used by the release manager
# to build official release tarballs, handle tagging and publish.
#
# do _NOT_ use for anything else!!!!!!!!!

## do sanity checks

ifndef VERSION

all:
	@echo WARNING: VERSION= is not defined!
	@exit 1

else ifndef OLDVER

all:
	@echo WARNING: OLDVER= is not defined!
	@exit 1

else

## setup stuff

PROJECT=cluster
PV=$(PROJECT)-$(VERSION)
TGZ=$(PV).tar.gz
TESTTGZ=TEST-$(TGZ)

all: test-tarball

test-tarball:
	git archive \
		--format=tar \
		--prefix=$(PV)/ \
		HEAD | \
		gzip -9 \
		> ../$(TESTTGZ)

release: tag tarballs

tag:
	git tag -a -m "$(PV) release" $(PV) HEAD

tarballs: master-tarball fence-agents-tarball resource-agents-tarball

master-tarball:
	git archive \
		--format=tar \
		--prefix=$(PV)/ \
		$(PV) | \
		tar xp
	sed -i -e \
		's#<CVS>#$(VERSION)#g' \
		$(PV)/gfs-kernel/src/gfs/gfs.h
	echo "VERSION \"$(VERSION)\"" \
		>> $(PV)/make/official_release_version
	tar cp $(PV) | \
		gzip -9 \
		> ../$(TGZ)
	rm -rf $(PV)

fence-agents-tarball:
	tar zxpf ../$(TGZ)
	mv $(PV) fence-agents-$(VERSION)
	cd fence-agents-$(VERSION) && \
		rm -rf bindings cman common config contrib dlm doc gfs* group rgmanager && \
		rm -rf fence/fenced fence/fence_node fence/fence_tool fence/include fence/libfence fence/libfenced && \
		rm -rf fence/man/fence.8 fence/man/fenced.8 fence/man/fence_node.8 fence/man/fence_tool.8
	tar cp fence-agents-$(VERSION) | \
		gzip -9 \
		> ../fence-agents-$(VERSION).tar.gz
	rm -rf fence-agents-$(VERSION)

resource-agents-tarball:
	tar zxpf ../$(TGZ)
	mv $(PV) resource-agents-$(VERSION)
	cd resource-agents-$(VERSION) && \
		rm -rf bindings cman common config contrib dlm doc fence gfs* group && \
		rm -rf rgmanager/ChangeLog rgmanager/errors.txt rgmanager/event-script.txt \
			rgmanager/examples rgmanager/include rgmanager/init.d rgmanager/man \
			rgmanager/README && \
		rm -rf rgmanager/src/clulib rgmanager/src/daemons rgmanager/src/utils
	tar cp resource-agents-$(VERSION) | \
		gzip -9 \
		> ../resource-agents-$(VERSION).tar.gz
	rm -rf resource-agents-$(VERSION)

publish: master-publish

master-publish:
	git push --tags origin
	scp ../$(TGZ) \
		fedorahosted.org:$(PROJECT)
	cp ../$(TGZ) \
		../ftp/$(TGZ)
	cd ../ftp && \
		cvs add $(TGZ) && \
		cvs commit -m "$(PV) release" $(TGZ)
	git log $(PROJECT)-$(OLDVER)..$(PV) | \
		git shortlog > ../$(PV).emaildata
	git diff --stat $(PROJECT)-$(OLDVER)..$(PV) \
		>> ../$(PV).emaildata
	@echo Hey you!.. yeah you looking somewhere else!
	@echo remember to update the wiki and send the email to cluster-devel and linux-cluster

endif
