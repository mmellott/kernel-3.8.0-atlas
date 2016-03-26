/*
 * cfg80211 debugfs
 *
 * Copyright 2009	Luis R. Rodriguez <lrodriguez@atheros.com>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include "core.h"
#include "debugfs.h"

#include "algorithm.h"
unsigned long iperf_rate;
EXPORT_SYMBOL(iperf_rate);
long force_cw;
EXPORT_SYMBOL(force_cw);


long dbg_psucc;
EXPORT_SYMBOL(dbg_psucc);


int is_cw_forced=-1;
EXPORT_SYMBOL(is_cw_forced);


#define DEBUGFS_READONLY_FILE(name, buflen, fmt, value...)		\
static ssize_t name## _read(struct file *file, char __user *userbuf,	\
			    size_t count, loff_t *ppos)			\
{									\
	struct wiphy *wiphy= file->private_data;		\
	char buf[buflen];						\
	int res;							\
									\
	res = scnprintf(buf, buflen, fmt "\n", ##value);		\
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);	\
}									\
									\
static const struct file_operations name## _ops = {			\
	.read = name## _read,						\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

DEBUGFS_READONLY_FILE(rts_threshold, 20, "%d",
		      wiphy->rts_threshold)
DEBUGFS_READONLY_FILE(fragmentation_threshold, 20, "%d",
		      wiphy->frag_threshold);
DEBUGFS_READONLY_FILE(short_retry_limit, 20, "%d",
		      wiphy->retry_short)
DEBUGFS_READONLY_FILE(long_retry_limit, 20, "%d",
		      wiphy->retry_long);

//char source_rate_buffer[32];

static int ht_print_chan(struct ieee80211_channel *chan,
			 char *buf, int buf_size, int offset)
{
	if (WARN_ON(offset > buf_size))
		return 0;

	if (chan->flags & IEEE80211_CHAN_DISABLED)
		return snprintf(buf + offset,
				buf_size - offset,
				"%d Disabled\n",
				chan->center_freq);

	return snprintf(buf + offset,
			buf_size - offset,
			"%d HT40 %c%c\n",
			chan->center_freq,
			(chan->flags & IEEE80211_CHAN_NO_HT40MINUS) ? ' ' : '-',
			(chan->flags & IEEE80211_CHAN_NO_HT40PLUS)  ? ' ' : '+');
}

static ssize_t ht40allow_map_read(struct file *file,
				  char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct wiphy *wiphy = file->private_data;
	char *buf;
	unsigned int offset = 0, buf_size = PAGE_SIZE, i, r;
	enum ieee80211_band band;
	struct ieee80211_supported_band *sband;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&cfg80211_mutex);

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;
		for (i = 0; i < sband->n_channels; i++)
			offset += ht_print_chan(&sband->channels[i],
						buf, buf_size, offset);
	}

	mutex_unlock(&cfg80211_mutex);

	r = simple_read_from_buffer(user_buf, count, ppos, buf, offset);

	kfree(buf);

	return r;
}

static const struct file_operations ht40allow_map_ops = {
	.read = ht40allow_map_read,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t source_rate_read(struct file *file, char __user *c_userbuf, size_t count, loff_t *ppos) {

	char reading_buf[32];
	int res;

	printk("atlas READING function\n");
	
	res = scnprintf(reading_buf, 32, "%lu" "\n", iperf_rate);

	printk("c_userbuf: %s\n", c_userbuf);
	printk("reading_buf: %s\n", reading_buf);

	return simple_read_from_buffer(c_userbuf, count, ppos, reading_buf, res);
}

static ssize_t source_rate_write(struct file *file, const char __user *c_buf, size_t count, loff_t *ppos) {

	char buf[32];
	ssize_t buf_len;
	unsigned long res;

	res=0;
	buf_len = min(count, sizeof(c_buf) - 1);

	printk("atlas WRITING function\n");
	printk("buf_len: %zu\n", buf_len);
	
	copy_from_user(buf, c_buf, buf_len);

	printk("res=%lu\n",res);
	printk("buf=%s\n", buf);

        if (copy_from_user(buf, c_buf, buf_len))
                return -EFAULT;

        buf[buf_len] = '\0';
        if (strict_strtoul(buf, 0, &res))
                return -EINVAL;

	iperf_rate=res;
        return count;


}

static const struct file_operations source_rate_ops = {

	.write = source_rate_write,
	.read = source_rate_read,
	.open = simple_open,
};

static ssize_t react_stats_read(struct file *file, char __user *c_userbuf, size_t count, loff_t *ppos) {

	char reading_buf[32];
	int res;

	printk("atlas READING function\n");
	
	res = scnprintf(reading_buf, 32, "%lu" "\n", iperf_rate);

	printk("c_userbuf: %s\n", c_userbuf);
	printk("reading_buf: %s\n", reading_buf);

	return simple_read_from_buffer(c_userbuf, count, ppos, reading_buf, res);
}

static ssize_t react_stats_write(struct file *file, const char __user *c_buf, size_t count, loff_t *ppos) {

	char buf[32];
	ssize_t buf_len;
	unsigned long res;

	res=0;
	buf_len = min(count, sizeof(c_buf) - 1);

	printk("atlas WRITING function\n");
	printk("buf_len: %zu\n", buf_len);
	
	copy_from_user(buf, c_buf, buf_len);

	printk("res=%lu\n",res);
	printk("buf=%s\n", buf);

        if (copy_from_user(buf, c_buf, buf_len))
                return -EFAULT;

        buf[buf_len] = '\0';
        if (strict_strtoul(buf, 0, &res))
                return -EINVAL;

	iperf_rate=res;
        return count;


}

static const struct file_operations react_stats_ops = {

	.write = react_stats_write,
	.read = react_stats_read,
	.open = simple_open,
};

static ssize_t read_file_cw(struct file *file, char __user *c_userbuf, size_t count, loff_t *ppos) {

	char reading_buf[32];
	int res;

	printk("read CW Value\n");
	
	res = scnprintf(reading_buf, 32, "%ld" "\n", force_cw);

	printk("c_userbuf: %s\n", c_userbuf);
	printk("reading_buf: %s\n", reading_buf);

	return simple_read_from_buffer(c_userbuf, count, ppos, reading_buf, res);
}

static ssize_t write_file_cw(struct file *file, const char __user *c_buf, size_t count, loff_t *ppos) {

	char buf[32];
	ssize_t buf_len;
	long res;

	res=0;
	buf_len = min(count, sizeof(c_buf) - 1);

	printk("atlas WRITING function\n");
	printk("buf_len: %zu\n", buf_len);
	
	copy_from_user(buf, c_buf, buf_len);

	printk("res=%ld\n",res);
	printk("buf=%s\n", buf);

        if (copy_from_user(buf, c_buf, buf_len))
                return -EFAULT;

        buf[buf_len] = '\0';
        if (strict_strtol(buf, 0, &res))

                return -EINVAL;

	force_cw=res;
	if (force_cw >=0){
		is_cw_forced=1;
		cw_update2(force_cw);
	}else{
		cw_update2(-1);
		is_cw_forced=-1;
	}
        return count;


}

static const struct file_operations fops_cw = {
	.write = write_file_cw,
	.read = read_file_cw,
	.open = simple_open,
};


static ssize_t read_file_psucc(struct file *file, char __user *c_userbuf, size_t count, loff_t *ppos) {

	char reading_buf[32];
	int res;

	printk("read psucc Value\n");
	
	res = scnprintf(reading_buf, 32, "%ld" "\n", dbg_psucc);

	printk("c_userbuf: %s\n", c_userbuf);
	printk("reading_buf: %s\n", reading_buf);

	return simple_read_from_buffer(c_userbuf, count, ppos, reading_buf, res);
}

static ssize_t write_file_psucc(struct file *file, const char __user *c_buf, size_t count, loff_t *ppos) {

	char buf[32];
	ssize_t buf_len;
	long res;

	res=0;
	buf_len = min(count, sizeof(c_buf) - 1);

	printk("buf_len: %zu\n", buf_len);
	
	copy_from_user(buf, c_buf, buf_len);

	printk("res=%ld\n",res);
	printk("buf=%s\n", buf);

        if (copy_from_user(buf, c_buf, buf_len))
                return -EFAULT;

        buf[buf_len] = '\0';
        if (strict_strtol(buf, 0, &res))

                return -EINVAL;

	dbg_psucc=res;

        return count;


}

static const struct file_operations fops_psucc = {
	.write = write_file_psucc,
	.read = read_file_psucc,
	.open = simple_open,
};






#define DEBUGFS_ADD(name)						\
	debugfs_create_file(#name, S_IRUGO, phyd, &rdev->wiphy, &name## _ops);

void cfg80211_debugfs_rdev_add(struct cfg80211_registered_device *rdev)
{
	struct dentry *phyd = rdev->wiphy.debugfsdir;

	DEBUGFS_ADD(rts_threshold);
	DEBUGFS_ADD(fragmentation_threshold);
	DEBUGFS_ADD(short_retry_limit);
	DEBUGFS_ADD(long_retry_limit);
	DEBUGFS_ADD(ht40allow_map);

	debugfs_create_file("source_rate", S_IRUGO | S_IWUGO, phyd, &rdev->wiphy, &source_rate_ops);
	debugfs_create_file("react_stats", S_IRUGO | S_IWUGO, phyd, &rdev->wiphy, &react_stats_ops);
	debugfs_create_file("cw", S_IRUGO | S_IWUGO, phyd, &rdev->wiphy, &fops_cw);
	debugfs_create_file("psucc", S_IRUGO | S_IWUGO, phyd, &rdev->wiphy, &fops_psucc);
}
