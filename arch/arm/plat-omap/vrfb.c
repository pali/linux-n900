#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/bitops.h>

#include <mach/io.h>
#include <mach/vrfb.h>

/*#define DEBUG*/

#ifdef DEBUG
#define DBG(format, ...) printk(KERN_DEBUG "VRFB: " format, ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

#define SMS_ROT_VIRT_BASE(context, rot) \
	(((context >= 4) ? 0xD0000000 : 0x70000000) \
	 + (0x4000000 * (context)) \
	 + (0x1000000 * (rot)))

#define OMAP_VRFB_SIZE			(2048 * 2048 * 4)

#define VRFB_PAGE_WIDTH_EXP	5 /* Assuming SDRAM pagesize= 1024 */
#define VRFB_PAGE_HEIGHT_EXP	5 /* 1024 = 2^5 * 2^5 */
#define VRFB_PAGE_WIDTH		(1 << VRFB_PAGE_WIDTH_EXP)
#define VRFB_PAGE_HEIGHT	(1 << VRFB_PAGE_HEIGHT_EXP)
#define SMS_IMAGEHEIGHT_OFFSET	16
#define SMS_IMAGEWIDTH_OFFSET	0
#define SMS_PH_OFFSET		8
#define SMS_PW_OFFSET		4
#define SMS_PS_OFFSET		0

#define OMAP_SMS_BASE		0x6C000000
#define SMS_ROT_CONTROL(context)	(OMAP_SMS_BASE + 0x180 + 0x10 * context)
#define SMS_ROT_SIZE(context)		(OMAP_SMS_BASE + 0x184 + 0x10 * context)
#define SMS_ROT_PHYSICAL_BA(context)	(OMAP_SMS_BASE + 0x188 + 0x10 * context)

#define VRFB_NUM_CTXS 12
/* bitmap of reserved contexts */
static unsigned long ctx_map;

/*
 * Access to this happens from client drivers or the PM core after wake-up.
 * For the first case we require locking at the driver level, for the second
 * we don't need locking, since no drivers will run until after the wake-up
 * has finished.
 */
static struct {
	u32 physical_ba;
	u32 control;
	u32 size;
} vrfb_hw_context[VRFB_NUM_CTXS];

static void inline restore_hw_context(int ctx)
{
	omap_writel(vrfb_hw_context[ctx].control, SMS_ROT_CONTROL(ctx));
	omap_writel(vrfb_hw_context[ctx].size, SMS_ROT_SIZE(ctx));
	omap_writel(vrfb_hw_context[ctx].physical_ba, SMS_ROT_PHYSICAL_BA(ctx));
}

/*
 * This the extra space needed in the VRFB physical area for VRFB to safely wrap
 * any memory accesses to the invisible part of the virtual view to the physical
 * area.
 */
static inline u32 get_extra_physical_size(u16 width, u8 bytespp)
{
	return (OMAP_VRFB_LINE_LEN - width) * bytespp * VRFB_PAGE_HEIGHT;
}

void omap_vrfb_restore_context(void)
{
	int i;
	unsigned long map = ctx_map;

	for (i = ffs(map); i; i = ffs(map)) {
		/* i=1..32 */
		i--;
		map &= ~(1 << i);
		restore_hw_context(i);
	}
}

void omap_vrfb_adjust_size(u16 *width, u16 *height,
		u8 bytespp)
{
	*width = ALIGN(*width * bytespp, VRFB_PAGE_WIDTH) / bytespp;
	*height = ALIGN(*height, VRFB_PAGE_HEIGHT);
}
EXPORT_SYMBOL(omap_vrfb_adjust_size);

u32 omap_vrfb_min_phys_size(u16 width, u16 height, u8 bytespp)
{
	/* mmap() is page aligned */
	height = ALIGN(OMAP_VRFB_LINE_LEN * bytespp * height, PAGE_SIZE) /
		(OMAP_VRFB_LINE_LEN * bytespp);

	omap_vrfb_adjust_size(&width, &height, bytespp);

	if (width > OMAP_VRFB_LINE_LEN)
		return 0;

	if (height > 2048)
		return 0;

	return (width * height * bytespp) + get_extra_physical_size(width, bytespp);
}
EXPORT_SYMBOL(omap_vrfb_min_phys_size);

u16 omap_vrfb_max_height(u32 phys_size, u16 width, u8 bytespp)
{
	unsigned long height;
	unsigned long extra;

	width = ALIGN(width * bytespp, VRFB_PAGE_WIDTH) / bytespp;

	if (width > OMAP_VRFB_LINE_LEN)
		return 0;

	extra = get_extra_physical_size(width, bytespp);

	if (phys_size < extra)
		return 0;

	height = (phys_size - extra) / (width * bytespp);

	/* mmap() is page aligned */
	height = (OMAP_VRFB_LINE_LEN * bytespp * height & ~PAGE_MASK) /
		(OMAP_VRFB_LINE_LEN * bytespp);

	/* Only full tiles */
	height &= ~(VRFB_PAGE_HEIGHT - 1);

	/* Virtual views provided by VRFB are limited to 2048x2048. */
	return min(height, 2048UL);
}
EXPORT_SYMBOL(omap_vrfb_max_height);

void omap_vrfb_setup(struct vrfb *vrfb, unsigned long paddr,
		u16 width, u16 height,
		enum omap_color_mode color_mode)
{
	unsigned pixel_size_exp;
	u16 vrfb_width;
	u16 vrfb_height;
	u8 ctx = vrfb->context;
	u8 bytespp;
	u32 size;
	u32 control;

	DBG("omapfb_set_vrfb(%d, %lx, %dx%d, %d)\n", ctx, paddr,
			width, height, color_mode);

	switch (color_mode) {
	case OMAP_DSS_COLOR_RGB16:
	case OMAP_DSS_COLOR_ARGB16:
		bytespp = 2;
		break;

	case OMAP_DSS_COLOR_RGB24P:
		bytespp = 3;
		break;

	case OMAP_DSS_COLOR_RGB24U:
	case OMAP_DSS_COLOR_ARGB32:
	case OMAP_DSS_COLOR_RGBA32:
	case OMAP_DSS_COLOR_RGBX32:
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
		bytespp = 4;
		break;

	default:
		BUG();
		return;
	}

	if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY)
		width >>= 1;

	if (bytespp == 4)
		pixel_size_exp = 2;
	else if (bytespp == 2)
		pixel_size_exp = 1;
	else
		BUG();

	vrfb_width = ALIGN(width * bytespp, VRFB_PAGE_WIDTH) / bytespp;
	vrfb_height = ALIGN(height, VRFB_PAGE_HEIGHT);

	DBG("vrfb w %u, h %u bytespp %d\n", vrfb_width, vrfb_height, bytespp);

	size  = vrfb_width << SMS_IMAGEWIDTH_OFFSET;
	size |= vrfb_height << SMS_IMAGEHEIGHT_OFFSET;

	control  = pixel_size_exp << SMS_PS_OFFSET;
	control |= VRFB_PAGE_WIDTH_EXP  << SMS_PW_OFFSET;
	control |= VRFB_PAGE_HEIGHT_EXP << SMS_PH_OFFSET;

	vrfb_hw_context[ctx].physical_ba = paddr;
	vrfb_hw_context[ctx].size = size;
	vrfb_hw_context[ctx].control = control;

	omap_writel(paddr, SMS_ROT_PHYSICAL_BA(ctx));
	omap_writel(size, SMS_ROT_SIZE(ctx));
	omap_writel(control, SMS_ROT_CONTROL(ctx));

	DBG("vrfb offset pixels %d, %d\n",
			vrfb_width - width, vrfb_height - height);

	vrfb->xres = width;
	vrfb->yres = height;
	vrfb->xoffset = vrfb_width - width;
	vrfb->yoffset = vrfb_height - height;
	vrfb->bytespp = bytespp;
}
EXPORT_SYMBOL(omap_vrfb_setup);

int omap_vrfb_map_angle(struct vrfb *vrfb, u16 height, u8 rot)
{
	unsigned long size = height * OMAP_VRFB_LINE_LEN * vrfb->bytespp;

	vrfb->vaddr[rot] = ioremap_wc(vrfb->paddr[rot], size);

	if (!vrfb->vaddr[rot]) {
		printk(KERN_ERR "vrfb: ioremap failed\n");
		return -ENOMEM;
	}

	DBG("ioremapped vrfb area %d of size %lu into %p\n", rot, size,
		vrfb->vaddr[rot]);

	return 0;
}
EXPORT_SYMBOL(omap_vrfb_map_angle);

void omap_vrfb_release_ctx(struct vrfb *vrfb)
{
	int rot;
	int ctx = vrfb->context;

	if (ctx == 0xff)
		return;

	DBG("release ctx %d\n", ctx);

	if (!(ctx_map & (1 << ctx))) {
		BUG();
		return;
	}
	clear_bit(ctx, &ctx_map);

	for (rot = 0; rot < 4; ++rot) {
		if(vrfb->paddr[rot]) {
			release_mem_region(vrfb->paddr[rot], OMAP_VRFB_SIZE);
			vrfb->paddr[rot] = 0;
		}
	}

	vrfb->context = 0xff;
}
EXPORT_SYMBOL(omap_vrfb_release_ctx);

int omap_vrfb_request_ctx(struct vrfb *vrfb)
{
	int rot;
	u32 paddr;
	u8 ctx;

	DBG("request ctx\n");

	for (ctx = 0; ctx < VRFB_NUM_CTXS; ++ctx)
		if ((ctx_map & (1 << ctx)) == 0)
			break;

	if (ctx == VRFB_NUM_CTXS) {
		printk(KERN_ERR "vrfb: no free contexts\n");
		return -EBUSY;
	}

	DBG("found free ctx %d\n", ctx);

	set_bit(ctx, &ctx_map);

	memset(vrfb, 0, sizeof(*vrfb));

	vrfb->context = ctx;

	for (rot = 0; rot < 4; ++rot) {
		paddr = SMS_ROT_VIRT_BASE(ctx, rot);
		if (!request_mem_region(paddr, OMAP_VRFB_SIZE, "vrfb")) {
			printk(KERN_ERR "vrfb: failed to reserve VRFB "
					"area for ctx %d, rotation %d\n",
					ctx, rot * 90);
			omap_vrfb_release_ctx(vrfb);
			return -ENOMEM;
		}

		vrfb->paddr[rot] = paddr;

		DBG("VRFB %d/%d: %lx\n", ctx, rot*90, vrfb->paddr[rot]);
	}

	return 0;
}
EXPORT_SYMBOL(omap_vrfb_request_ctx);

