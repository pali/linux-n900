/*
 * OMAP Bootreason passing
 *
 * Copyright (c) 2004-2005 Nokia
 * Copyright (c) 2013      Pali Rohár <pali.rohar@gmail.com>
 *
 * Written by David Weinehall <david.weinehall@nokia.com>
 * Written by Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/proc_fs.h>
#include <linux/errno.h>

static char boot_reason[16] = "pwr_key";

static int bootreason_read_proc(char *page, char **start, off_t off,
					 int count, int *eof, void *data)
{
	int len = 0;

	len += sprintf(page + len, "%s\n", boot_reason);

	*start = page + off;

	if (len > off)
		len -= off;
	else
		len = 0;

	return len < count ? len  : count;
}

static int __init bootreason_init(void)
{
	printk(KERN_INFO "Bootup reason: %s\n", boot_reason);

	if (!create_proc_read_entry("bootreason", S_IRUGO, NULL,
					bootreason_read_proc, NULL))
		return -ENOMEM;

	return 0;
}

late_initcall(bootreason_init);

struct version_config {
	char component[12];
	char version[12];
};

static int version_configs_n = 4;
static struct version_config version_configs[] = {
	{"product", "RX-51"},
	{"hw-build", "2101"},
	{"nolo", "1.4.14"},
	{"boot-mode", "normal"},
};

static int component_version_read_proc(char *page, char **start, off_t off,
				       int count, int *eof, void *data)
{
	int len, i;
	const struct version_config *ver;
	char *p;

	i = 0;
	p = page;
	for (i = 0; i< version_configs_n; i++) {
		ver = &version_configs[i];
		p += sprintf(p, "%-12s%s\n", ver->component, ver->version);
	}

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}

static int __init component_version_init(void)
{
	if (!create_proc_read_entry("component_version", S_IRUGO, NULL,
				    component_version_read_proc, NULL))
		return -ENOMEM;

	return 0;
}

late_initcall(component_version_init);
