#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0x9de7765d, "module_layout" },
	{ 0x3bd5c9b1, "usb_deregister" },
	{ 0x1a62a191, "usb_register_driver" },
	{ 0x962c8ae1, "usb_kill_anchored_urbs" },
	{ 0x262dd71c, "_dev_warn" },
	{ 0x407af304, "usb_wait_anchor_empty_timeout" },
	{ 0x378a81c2, "input_unregister_device" },
	{ 0x296695f, "refcount_warn_saturate" },
	{ 0x37a0cba, "kfree" },
	{ 0x7710f9c4, "usb_put_dev" },
	{ 0x3a81bb0a, "usb_put_intf" },
	{ 0x115d4056, "usb_free_urb" },
	{ 0x405f2095, "_dev_info" },
	{ 0xf6d07d4, "input_register_device" },
	{ 0xe7efa52a, "input_set_capability" },
	{ 0xf6bdf445, "input_allocate_device" },
	{ 0x51846891, "usb_free_coherent" },
	{ 0x37f1375c, "usb_alloc_urb" },
	{ 0x890b2231, "usb_alloc_coherent" },
	{ 0xf9c0b663, "strlcat" },
	{ 0x656e4a6e, "snprintf" },
	{ 0x977f511b, "__mutex_init" },
	{ 0xb5f17439, "kmem_cache_alloc_trace" },
	{ 0xcbf895e0, "kmalloc_caches" },
	{ 0xde1a3fdb, "usb_kill_urb" },
	{ 0xbb30cbc5, "__dynamic_dev_dbg" },
	{ 0x3812050a, "_raw_spin_unlock_irqrestore" },
	{ 0x51760917, "_raw_spin_lock_irqsave" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0x43388a0b, "_dev_err" },
	{ 0xb0d9a8a7, "input_event" },
	{ 0x4e83d079, "usb_submit_urb" },
	{ 0xc5850110, "printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("usb:v046DpC21Dd*dc*dsc*dp*ic*isc*ip*in*");

MODULE_INFO(srcversion, "0C3D0FC57EE172BC8DC6975");
