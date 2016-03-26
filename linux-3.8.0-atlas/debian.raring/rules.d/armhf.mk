human_arch	= ARM (hard float)
build_arch	= arm
header_arch	= arm
defconfig	= defconfig
flavours	= generic
build_image	= zImage
kernel_file	= arch/$(build_arch)/boot/zImage
install_file	= vmlinuz
no_dumpfile	= true

skipabi         = true
skipmodule      = true
do_libc_dev_package=false
do_tools        = true
do_tools_perf   = true
do_doc_package  = false
do_source_package=false

# Flavour specific configuration.
dtb_files_generic	= imx6q-sabrelite.dtb omap3-beagle-xm.dtb omap4-panda.dtb omap4-panda-es.dtb highbank.dtb
