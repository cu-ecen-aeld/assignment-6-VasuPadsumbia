# See https://git.yoctoproject.org/poky/tree/meta/files/common-licenses
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# TODO: Set this  with the path to your assignments rep.  Use ssh protocol and see lecture notes
# about how to setup ssh-agent for passwordless access
SRC_URI = "git://git@github.com/cu-ecen-aeld/assignment-6-VasuPadsumbia.git;protocol=ssh;branch=main"

PV = "1.0+git${SRCPV}"
# TODO: set to reference a specific commit hash in your assignment repo
SRCREV = "205dc433786a6e695633714164534a24670e4714"

# This sets your staging directory based on WORKDIR, where WORKDIR is defined at 
# https://docs.yoctoproject.org/ref-manual/variables.html?highlight=workdir#term-WORKDIR
# We reference the "server" directory here to build from the "server" directory
# in your assignments repo
S = "${WORKDIR}/git/server"

# TODO: Add the aesdsocket application and any other files you need to install
# See https://git.yoctoproject.org/poky/plain/meta/conf/bitbake.conf?h=kirkstone
FILES:${PN} += "${bindir}/aesdsocket"
FILES:${PN} += "${bindir}/socket.h"
FILES:${PN} += "${bindir}/socket.c"
FILES:${PN} += "${bindir}/aesdsocket.c"
FILES:${PN} += "${bindir}/Makefile"
FILES:${PN} += "${sysconfdir}/init.d/S99aesdsocket"

inherit update-rc.d
# This sets the name of the init script to be installed in /etc/init.d
# and the parameters to be passed to the update-rc.d command
# See https://git.yoctoproject.org/poky/plain/meta/classes/update-rc.d?h=kirkstone
# and https://docs.yoctoproject.org/ref-manual/variables.html?highlight=workdir#term-SYSVINIT_SCRIPT

INITSCRIPT_NAME = "S99aesdsocket" 
INITSCRIPT_PARAMS = "defaults"

# TODO: customize these as necessary for any libraries you need for your application
# (and remove comment)
TARGET_LDFLAGS += "-pthread -lrt"

do_configure () {
	:
}

do_compile () {
	oe_runmake
}

do_install () {
	# TODO: Install your binaries/scripts here.
	install -d ${D}${bindir}
	install -m 0755 ${S}/aesdsocket ${D}${bindir}/aesdsocket
	install -m 0644 ${S}/socket.h ${D}${bindir}/socket.h
	install -m 0644 ${S}/socket.c ${D}${bindir}/socket.c
	install -m 0644 ${S}/aesdsocket.c ${D}${bindir}/aesdsocket.c
	install -m 0644 ${S}/Makefile ${D}${bindir}/Makefile
	install -d ${D}${sysconfdir}/init.d
	install -m 0755 ${S}/aesdsocket-start-stop.sh ${D}${sysconfdir}/init.d/S99aesdsocket
	# Be sure to install the target directory with install -d first
	# Yocto variables ${D} and ${S} are useful here, which you can read about at 
	# https://docs.yoctoproject.org/ref-manual/variables.html?highlight=workdir#term-D
	# and
	# https://docs.yoctoproject.org/ref-manual/variables.html?highlight=workdir#term-S
	# See example at https://github.com/cu-ecen-aeld/ecen5013-yocto/blob/ecen5013-hello-world/meta-ecen5013/recipes-ecen5013/ecen5013-hello-world/ecen5013-hello-world_git.bb
}
