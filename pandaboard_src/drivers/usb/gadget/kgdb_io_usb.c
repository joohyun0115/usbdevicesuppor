/*
 * KGDB I/O USB
 * This file is based in part on 
 * 		kgdboc in Linux Kernel
 *
 * Copyright (C) 2011 Sevencore, Inc.
 * Author: Joohyun Kyong <joohyun0115@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/kgdb.h>
#include <linux/tty.h>
#include <linux/console.h>

#include "f_kgdb.h"

#define MAX_CONFIG_LEN		40

static struct kgdb_io		kgdb_io_usb_io_ops;

/* -1 = init not run yet, 0 = unconfigured, 1 = configured. */
static int configured		= -1;

static char config[MAX_CONFIG_LEN];
static struct kparam_string kps = {
	.string			= config,
	.maxlen			= MAX_CONFIG_LEN,
};

static int kgdb_io_usb_option_setup(char *opt)
{
	if (strlen(opt) > MAX_CONFIG_LEN) {
		printk(KERN_ERR "kgdb_io_usb: config string too long\n");
		return -ENOSPC;
	}
	strcpy(config, opt);

	return 0;
}

__setup("kgdb_io_usb=", kgdb_io_usb_option_setup);

static int configure_kgdb_io_usb(void)
{
	int err;

	err = kgdb_register_io_module(&kgdb_io_usb_io_ops);
	if (err)
		goto noconfig;

	configured = 1;

	return 0;

noconfig:
	config[0] = 0;
	configured = 0;

	return err;
}


#define BUFFER_SIZE 4096	 
#define END_PUT_COUNT 3

static int put_count;
static int check = 0;
static char kgdb_read_buf[BUFFER_SIZE];
static char kgdb_write_buf[BUFFER_SIZE];

enum cqueue_type {
	RX = 0,
	TX = 1,
};

struct cqueue {
	unsigned char queue[BUFFER_SIZE];
	int top;
	int bottom;
};

struct cqueue cqueue_buffer[2]; 

static int is_empty(int type)
{
	return (cqueue_buffer[type].top == cqueue_buffer[type].bottom) ;
}

static void add_queue(int type, unsigned char data)
{
	cqueue_buffer[type].queue[cqueue_buffer[type].top] = data;
	cqueue_buffer[type].top = (cqueue_buffer[type].top + 1) % BUFFER_SIZE;
}

static unsigned char delete_queue(int type)
{
	unsigned char data;

	data = cqueue_buffer[type].queue[cqueue_buffer[type].bottom];
	cqueue_buffer[type].bottom = (cqueue_buffer[type].bottom + 1) % BUFFER_SIZE;

	return data;
}

static void buffer_init(void)
{
	cqueue_buffer[RX].top = cqueue_buffer[RX].bottom = 0;
	cqueue_buffer[TX].top = cqueue_buffer[TX].bottom = 0;
}

static int boot_break;

static void kgdb_set_boot_break(void)
{
	boot_break = 1;
}
EXPORT_SYMBOL(kgdb_set_boot_break);

static int __init init_kgdb_io_usb(void)
{
	printk("init_kgdb_io_usb\n");
	/* Already configured? */
	if (configured == 1)
		return 0;

	buffer_init();
	return configure_kgdb_io_usb();
}

static void cleanup_kgdb_io_usb(void)
{
	if (configured == 1)
		kgdb_unregister_io_module(&kgdb_io_usb_io_ops);
}


static int kgdb_io_usb_get_char(void)
{
	int i;
	int size;
	char read_value;

	if (!is_empty(RX)) {
		read_value = delete_queue(RX);
		return (int) read_value;
	}

	size = kgdb_read(kgdb_read_buf, BUFFER_SIZE);

	for (i = 0; i < size; i++) 
		add_queue(RX, kgdb_read_buf[i]);

	read_value = delete_queue(RX);	

	return (int)read_value;
}

static void kgdb_io_usb_put_char(u8 chr)
{

	add_queue(TX, chr);

	if (chr == '#') {
		check = 1;
	}

	if (check)
		put_count++;

	if (put_count == END_PUT_COUNT) {
		while (!is_empty(TX)) 
			kgdb_write_buf[put_count++] = delete_queue(TX);

		kgdb_write(kgdb_write_buf, put_count);

		check = 0;
		put_count = 0;
	}
}

static int param_set_kgdb_io_usb_var(const char *kmessage, struct kernel_param *kp)
{
	int len = strlen(kmessage);

	if (len >= MAX_CONFIG_LEN) {
		printk(KERN_ERR "kgdb_io_usb: config string too long\n");
		return -ENOSPC;
	}

	/* Only copy in the string if the init function has not run yet */
	if (configured < 0) {
		strcpy(config, kmessage);
		return 0;
	}

	if (kgdb_connected) {
		printk(KERN_ERR
				"kgdb_io_usb: Cannot reconfigure while KGDB is connected.\n");

		return -EBUSY;
	}

	strcpy(config, kmessage);
	/* Chop out \n char as a result of echo */
	if (config[len - 1] == '\n')
		config[len - 1] = '\0';

	if (configured == 1)
		cleanup_kgdb_io_usb();

	/* Go and configure with the new params. */
	return configure_kgdb_io_usb();
}

static void kgdb_io_usb_pre_exp_handler(void)
{
	/* Increment the module count when the debugger is active */
	if (!kgdb_connected)
		try_module_get(THIS_MODULE);
}

static void kgdb_io_usb_post_exp_handler(void)
{
	/* decrement the module count when the debugger detaches */
	if (!kgdb_connected)
		module_put(THIS_MODULE);
}

static struct kgdb_io kgdb_io_usb_io_ops = {
	.name			= "kgdb_io_usb",
	.read_char		= kgdb_io_usb_get_char,
	.write_char		= kgdb_io_usb_put_char,
	.pre_exception		= kgdb_io_usb_pre_exp_handler,
	.post_exception		= kgdb_io_usb_post_exp_handler,
};

module_init(init_kgdb_io_usb);
module_exit(cleanup_kgdb_io_usb);
module_param_call(kgdb_io_usb, param_set_kgdb_io_usb_var, param_get_string, &kps, 0644);
MODULE_PARM_DESC(kgdb_io_usb, "<usb device>");
MODULE_DESCRIPTION("KGDB USB I/O Driver");
MODULE_LICENSE("GPL");
