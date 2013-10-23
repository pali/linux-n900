#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <asm/io.h>

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
	 | 0x4000000 * (context) \
	 | 0x1000000 * (rot))

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
static unsigned ctx_map;

void omap_vrfb_adjust_size(u16 *width, u16 *height,
		u8 bytespp)
{
	*width = ALIGN(*width * bytespp, VRFB_PAGE_WIDTH) / bytespp;
	*height = ALIGN(*height, VRFB_PAGE_HEIGHT);
}
EXPORT_SYMBOL(omap_vrfb_adjust_size);

void omap_vrfb_setup(struct vrfb *vrfb, unsigned long paddr,
		u16 width, u16 height,
		u8 bytespp)
{
	unsigned pixel_size_exp;
	u16 vrfb_width;
	u16 vrfb_height;
	u8 ctx = vrfb->context;

	DBG("omapfb_set_vrfb(%d, %lx, %dx%d, %d)\n", ctx, paddr,
			width, height, bytespp);

	if (bytespp == 4)
		pixel_size_exp = 2;
	else if (bytespp == 2)
		pixel_size_exp = 1;
	else
		BUG();

	vrfb_width = ALIGN(width * bytespp, VRFB_PAGE_WIDTH) / bytespp;
	vrfb_height = ALIGN(height, VRFB_PAGE_HEIGHT);

	DBG("vrfb w %u, h %u\n", vrfb_width, vrfb_height);

	omap_writel(paddr, SMS_ROT_PHYSICAL_BA(ctx));
	omap_writel((vrfb_width << SMS_IMAGEWIDTH_OFFSET) |
			(vrfb_height << SMS_IMAGEHEIGHT_OFFSET),
			SMS_ROT_SIZE(ctx));

	omap_writel(pixel_size_exp << SMS_PS_OFFSET |
			VRFB_PAGE_WIDTH_EXP  << SMS_PW_OFFSET |
			VRFB_PAGE_HEIGHT_EXP << SMS_PH_OFFSET,
			SMS_ROT_CONTROL(ctx));

	DBG("vrfb offset pixels %d, %d\n",
			vrfb_width - width, vrfb_height - height);

	vrfb->xoffset = vrfb_width - width;
	vrfb->yoffset = vrfb_height - height;
	vrfb->bytespp = bytespp;
}
EXPORT_SYMBOL(omap_vrfb_setup);

void omap_vrfb_release_ctx(struct vrfb *vrfb)
{
	int rot;

	if (vrfb->context == 0xff)
		return;

	DBG("release ctx %d\n", vrfb->context);

	ctx_map &= ~(1 << vrfb->context);

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

	ctx_map |= 1 << ctx;

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

