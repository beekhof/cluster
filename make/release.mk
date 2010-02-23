# NOTE: this make file snippet is only used by the release managers
# to build official release tarballs, handle tagging and publish.
#
# this script is NOT "make -j" safe
#
# do _NOT_ use for anything else!!!!!!!!!

# setup tons of vars

# signing key
gpgsignkey=0x6CE95CA7

# project layout
project=cluster
projectver=$(project)-$(version)
projecttar=$(projectver).tar
projectgz=$(projecttar).gz
projectbz=$(projecttar).bz2

fenceproject=fence-agents
fenceprojectver=$(fenceproject)-$(version)
fenceprojecttar=$(fenceprojectver).tar
fenceprojectgz=$(fenceprojecttar).gz
fenceprojectbz=$(fenceprojecttar).bz2

rasproject=resource-agents
rasprojectver=$(rasproject)-$(version)
rasprojecttar=$(rasprojectver).tar
rasprojectgz=$(rasprojecttar).gz
rasprojectbz=$(rasprojecttar).bz2

rgmproject=rgmanager
rgmprojectver=$(rgmproject)-$(version)
rgmprojecttar=$(rgmprojectver).tar
rgmprojectgz=$(rgmprojecttar).gz
rgmprojectbz=$(rgmprojecttar).bz2

# temp dirs

ifdef release
reldir=release
gitver=$(projectver)
forceclean=clean
else
reldir=release-candidate
gitver=HEAD
forceclean=
endif

releasearea=$(shell pwd)/../$(projectver)-$(reldir)

all: $(forceclean) checks setup tag tarballs changelog sha256 sign

checks:
ifeq (,$(version))
	@echo ERROR: need to define version=
	@exit 1
endif
ifeq (,$(oldversion))
	@echo ERROR: need to define oldversion=
	@exit 1
endif
	@if [ ! -d .git ]; then \
		echo This script needs to be executed from top level cluster git tree; \
		exit 1; \
	fi

setup: checks $(releasearea)

$(releasearea):
	mkdir $@

tag: setup $(releasearea)/tag-$(version)

$(releasearea)/tag-$(version):
ifeq (,$(release))
	@echo Building test release $(version), no tagging
else
	git tag -a -m "$(projectver) release" $(projectver) HEAD
endif
	@touch $@

tarballs: tag
tarballs: $(releasearea)/$(projecttar)
tarballs: $(releasearea)/$(projectgz)
tarballs: $(releasearea)/$(projectbz)
tarballs: $(releasearea)/$(fenceprojecttar)
tarballs: $(releasearea)/$(fenceprojectgz)
tarballs: $(releasearea)/$(fenceprojectbz)
tarballs: $(releasearea)/$(rasprojecttar)
tarballs: $(releasearea)/$(rasprojectgz)
tarballs: $(releasearea)/$(rasprojectbz)
tarballs: $(releasearea)/$(rgmprojecttar)
tarballs: $(releasearea)/$(rgmprojectgz)
tarballs: $(releasearea)/$(rgmprojectbz)

$(releasearea)/$(projecttar):
	@echo Creating $(project) tarball
	rm -rf $(releasearea)/$(projectver)
	git archive \
		--format=tar \
		--prefix=$(projectver)/ \
		$(gitver) | \
		(cd $(releasearea)/ && tar xf -)
	cd $(releasearea) && \
	sed -i -e \
		's#<CVS>#$(version)#g' \
		$(projectver)/gfs-kernel/src/gfs/gfs.h && \
	echo "VERSION \"$(version)\"" \
		>> $(projectver)/make/official_release_version && \
	tar cpf $(projecttar) $(projectver) && \
	rm -rf $(projectver)

$(releasearea)/$(fenceprojecttar): $(releasearea)/$(projecttar)
	@echo Creating $(fenceproject) tarball
	cd $(releasearea) && \
	rm -rf $(projectver) $(fenceprojectver) && \
	tar xpf $(projecttar) && \
	mv $(projectver) $(fenceprojectver) && \
	cd $(fenceprojectver) && \
	rm -rf bindings cman common config contrib dlm gfs* group \
		rgmanager fence/fenced fence/fence_node \
		fence/fence_tool fence/include fence/libfence \
		fence/libfenced fence/man && \
	cd .. && \
	tar cpf $(fenceprojecttar) $(fenceprojectver) && \
	rm -rf $(fenceprojectver)

$(releasearea)/$(rasprojecttar): $(releasearea)/$(projecttar)
	@echo Creating $(rasproject) tarball
	cd $(releasearea) && \
	rm -rf $(projectver) $(rasprojectver) && \
	tar xpf $(projecttar) && \
	mv $(projectver) $(rasprojectver) && \
	cd $(rasprojectver) && \
	rm -rf bindings cman common config contrib dlm fence gfs* \
		group rgmanager/ChangeLog rgmanager/errors.txt \
		rgmanager/event-script.txt rgmanager/examples \
		rgmanager/include rgmanager/init.d rgmanager/man \
		rgmanager/README rgmanager/src/clulib \
		rgmanager/src/daemons rgmanager/src/utils && \
	cd .. && \
	tar cpf $(rasprojecttar) $(rasprojectver) && \
	rm -rf $(rasprojectver)

$(releasearea)/$(rgmprojecttar): $(releasearea)/$(projecttar)
	@echo Creating $(rgmproject) tarball
	cd $(releasearea) && \
	rm -rf $(projectver) $(rgmprojectver) && \
	tar xpf $(projecttar) && \
	mv $(projectver) $(rgmprojectver) && \
	cd $(rgmprojectver) && \
	rm -rf bindings cman common config contrib dlm fence gfs* group \
		rgmanager/src/resources && \
	cd .. && \
	tar cpf $(rgmprojecttar) $(rgmprojectver) && \
	rm -rf $(rgmprojectver)

$(releasearea)/%.gz: $(releasearea)/%
	@echo Creating $@
	cat $< | gzip -9 > $@

$(releasearea)/%.bz2: $(releasearea)/%
	@echo Creating $@
	cat $< | bzip2 -c > $@

changelog: checks setup $(releasearea)/Changelog-$(version)

$(releasearea)/Changelog-$(version): $(releasearea)/$(projecttar)
	git log $(project)-$(oldversion)..$(gitver) | \
	git shortlog > $@
	git diff --stat $(project)-$(oldversion)..$(gitver) >> $@

sha256: changelog tarballs $(releasearea)/$(projectver).sha256

$(releasearea)/$(projectver).sha256: $(releasearea)/Changelog-$(version)
	cd $(releasearea) && \
	sha256sum Changelog-$(version) *.gz *.bz2 | sort -k2 > $@

sign: sha256 $(releasearea)/$(projectver).sha256.asc

$(releasearea)/$(projectver).sha256.asc: $(releasearea)/$(projectver).sha256
	cd $(releasearea) && \
	gpg --default-key $(gpgsignkey) \
		--detach-sign \
		--armor \
		$<

publish: sign
ifeq (,$(release))
	@echo Nothing to publish
else
	git push --tags origin
	cd $(releasearea) && \
	scp *.gz *.bz2 Changelog-* *sha256* \
		fedorahosted.org:$(project)
	@echo Hey you!.. yeah you looking somewhere else!
	@echo remember to update the wiki and send the email to cluster-devel and linux-cluster
endif

clean: checks
	rm -rf $(releasearea)
