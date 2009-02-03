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

MASTERPROJECT=cluster
MASTERPV=$(MASTERPROJECT)-$(VERSION)
MASTERTGZ=$(MASTERPV).tar.gz
TESTTGZ=TEST-$(MASTERTGZ)

# fence-agents
FENCEPROJECT=fence-agents
FENCEPV=$(FENCEPROJECT)-$(VERSION)
FENCETGZ=$(FENCEPV).tar.gz

# resource-agents
RASPROJECT=resource-agents
RASPV=$(RASPROJECT)-$(VERSION)
RASTGZ=$(RASPV).tar.gz

all: test-tarball

test-tarball:
	git archive \
		--format=tar \
		--prefix=$(MASTERPV)/ \
		HEAD | \
		gzip -9 \
		> ../$(TESTTGZ)

release: tag tarballs

tag:
	git tag -a -m "$(MASTERPV) release" $(MASTERPV) HEAD

tarballs: master-tarball fence-agents-tarball resource-agents-tarball

master-tarball:
	git archive \
		--format=tar \
		--prefix=$(MASTERPV)/ \
		$(MASTERPV) | \
		tar xp
	sed -i -e \
		's#<CVS>#$(VERSION)#g' \
		$(MASTERPV)/gfs-kernel/src/gfs/gfs.h
	echo "VERSION \"$(VERSION)\"" \
		>> $(MASTERPV)/make/official_release_version
	tar cp $(MASTERPV) | \
		gzip -9 \
		> ../$(MASTERTGZ)
	rm -rf $(MASTERPV)

fence-agents-tarball:
	tar zxpf ../$(MASTERTGZ)
	mv $(MASTERPV) $(FENCEPV)
	cd $(FENCEPV) && \
		rm -rf bindings cman common config contrib dlm doc gfs* group rgmanager && \
		rm -rf fence/fenced fence/fence_node fence/fence_tool fence/include fence/libfence fence/libfenced && \
		rm -rf fence/man/fence.8 fence/man/fenced.8 fence/man/fence_node.8 fence/man/fence_tool.8
	tar cp $(FENCEPV) | \
		gzip -9 \
		> ../$(FENCETGZ)
	rm -rf $(FENCEPV)

resource-agents-tarball:
	tar zxpf ../$(MASTERTGZ)
	mv $(MASTERPV) $(RASPV)
	cd $(RASPV) && \
		rm -rf bindings cman common config contrib dlm doc fence gfs* group && \
		rm -rf rgmanager/ChangeLog rgmanager/errors.txt rgmanager/event-script.txt \
			rgmanager/examples rgmanager/include rgmanager/init.d rgmanager/man \
			rgmanager/README && \
		rm -rf rgmanager/src/clulib rgmanager/src/daemons rgmanager/src/utils
	tar cp $(RASPV) | \
		gzip -9 \
		> ../$(RASTGZ)
	rm -rf $(RASPV)

publish:
	git push --tags origin
	scp ../$(MASTERTGZ) \
		fedorahosted.org:$(MASTERPROJECT)
	scp ../$(FENCETGZ) \
		fedorahosted.org:$(MASTERPROJECT)
	scp ../$(RASTGZ) \
		fedorahosted.org:$(MASTERPROJECT)
	git log $(MASTERPROJECT)-$(OLDVER)..$(MASTERPV) | \
		git shortlog > ../$(MASTERPV).emaildata
	git diff --stat $(MASTERPROJECT)-$(OLDVER)..$(MASTERPV) \
		>> ../$(MASTERPV).emaildata
	@echo Hey you!.. yeah you looking somewhere else!
	@echo remember to update the wiki and send the email to cluster-devel and linux-cluster

endif
