/*
 * MUSB OTG driver - support for Mentor's DMA controller
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2007 by Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include "musb_core.h"
#include "musbhsdma.h"

static int dma_controller_start(struct dma_controller *c)
{
	/* nothing to do */
	return 0;
}

static void dma_channel_release(struct dma_channel *channel);

static int dma_controller_stop(struct dma_controller *c)
{
	struct musb_dma_controller *controller = container_of(c,
			struct musb_dma_controller, controller);
	struct musb *musb = controller->private_data;
	struct dma_channel *channel;
	u8 bit;

	if (controller->used_channels != 0) {
		dev_err(musb->controller,
			"Stopping DMA controller while channel active\n");

		for (bit = 0; bit < MUSB_HSDMA_CHANNELS; bit++) {
			if (controller->used_channels & (1 << bit)) {
				channel = &controller->channel[bit].channel;
				dma_channel_release(channel);

				if (!controller->used_channels)
					break;
			}
		}
	}

	return 0;
}

static struct dma_channel *dma_channel_allocate(struct dma_controller *c,
				struct musb_hw_ep *hw_ep, u8 transmit)
{
	struct musb_dma_controller *controller = container_of(c,
			struct musb_dma_controller, controller);
	struct musb_dma_channel *musb_channel = NULL;
	struct dma_channel *channel = NULL;
	u8 bit;

	for (bit = 0; bit < MUSB_HSDMA_CHANNELS; bit++) {
		if (!(controller->used_channels & (1 << bit))) {
			controller->used_channels |= (1 << bit);
			musb_channel = &(controller->channel[bit]);
			musb_channel->controller = controller;
			musb_channel->idx = bit;
			musb_channel->epnum = hw_ep->epnum;
			musb_channel->transmit = transmit;
			channel = &(musb_channel->channel);
			channel->private_data = musb_channel;
			channel->status = MUSB_DMA_STATUS_FREE;
			channel->max_len = 0x100000;
			/* Tx => mode 1; Rx => mode 0 */
			channel->desired_mode = transmit;
			channel->actual_len = 0;
			break;
		}
	}

	return channel;
}

static void dma_channel_release(struct dma_channel *channel)
{
	struct musb_dma_channel *musb_channel = channel->private_data;

	channel->actual_len = 0;
	musb_channel->start_addr = 0;
	musb_channel->len = 0;

	musb_channel->controller->used_channels &=
		~(1 << musb_channel->idx);

	channel->status = MUSB_DMA_STATUS_UNKNOWN;
}

static void configure_channel(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	struct musb_dma_channel *musb_channel = channel->private_data;
	struct musb_dma_controller *controller = musb_channel->controller;
	void __iomem *mbase = controller->base;
	u8 bchannel = musb_channel->idx;
	u16 csr = 0;

	DBG(4, "%p, pkt_sz %d, addr 0x%x, len %d, mode %d\n",
			channel, packet_sz, dma_addr, len, mode);

	if (mode)
		csr |= MUSB_HSDMA_MODE1;

	csr |= (musb_channel->epnum << MUSB_HSDMA_ENDPOINT_SHIFT)
		| MUSB_HSDMA_BURSTMODE_INCR16
		| MUSB_HSDMA_ENABLE
		| MUSB_HSDMA_IRQENABLE
		| (musb_channel->transmit
				? MUSB_HSDMA_TRANSMIT
				: 0);

	if (musb_channel->transmit)
		controller->tx_active |= (1 << bchannel);
	else
		controller->rx_active |= (1 << bchannel);

	/* address/count */
	musb_write_hsdma_addr(mbase, bchannel, dma_addr);
	musb_write_hsdma_count(mbase, bchannel, len);

	/* control (this should start things) */
	musb_writew(mbase,
		MUSB_HSDMA_CHANNEL_OFFSET(bchannel, MUSB_HSDMA_CONTROL),
		csr);
}

static int dma_channel_program(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	struct musb_dma_channel *musb_channel = channel->private_data;
	struct musb_dma_controller *controller = musb_channel->controller;
	struct musb *musb = controller->private_data;

	DBG(2, "ep%d-%s pkt_sz %d, dma_addr 0x%x length %d, mode %d\n",
		musb_channel->epnum,
		musb_channel->transmit ? "Tx" : "Rx",
		packet_sz, dma_addr, len, mode);

	BUG_ON(channel->status == MUSB_DMA_STATUS_UNKNOWN ||
		channel->status == MUSB_DMA_STATUS_BUSY);

	/*
	 * on MUSB RTL 1.8 and above, DMA has to be word aligned
	 * Fail dma programming here so that we fallback to pio
	 */
	if ((dma_addr & 0x03) &&
		(musb->hwvers >= MUSB_HWVERS_1800))
		return false;

	/* In version 1.4, if two DMA channels are simultaneously
	 * enabled in opposite directions, there is a chance that
	 * the DMA controller will hang. However, it is safe to
	 * have multiple DMA channels enabled in the same direction
	 * at the same time.
	 */
	if (musb->hwvers == MUSB_HWVERS_1400) {
		if (musb_channel->transmit && controller->rx_active)
			return false;
		else if	(!musb_channel->transmit && controller->tx_active)
			return false;
	}

	channel->actual_len = 0;
	musb_channel->start_addr = dma_addr;
	musb_channel->len = len;
	musb_channel->max_packet_sz = packet_sz;
	channel->status = MUSB_DMA_STATUS_BUSY;

	/*
	 * Due to an ASIC bug, we cannot use AUTOCLEAR for receive channels, so
	 * configure DMA for one packet at a time. RXPKTRDY will be cleared
	 * manually and the transfer continued from the DMA interrupt.
	 */
	if (!musb_channel->transmit && musb->autoclear_wa)
		len = musb_channel->max_packet_sz;

	configure_channel(channel, packet_sz, mode, dma_addr, len);

	return true;
}

static int dma_channel_abort(struct dma_channel *channel)
{
	struct musb_dma_channel *musb_channel = channel->private_data;
	void __iomem *mbase = musb_channel->controller->base;

	u32 addr = 0;
	u8 bchannel = musb_channel->idx;
	int offset;

	if (channel->status == MUSB_DMA_STATUS_BUSY) {
		if (musb_channel->transmit) {
			offset = MUSB_EP_OFFSET(musb_channel->epnum,
						MUSB_TXCSR);
		} else {
			offset = MUSB_EP_OFFSET(musb_channel->epnum,
						MUSB_RXCSR);
			addr = musb_read_hsdma_addr(mbase, bchannel);
			channel->actual_len = addr - musb_channel->start_addr;
		}

		musb_writew(mbase,
			MUSB_HSDMA_CHANNEL_OFFSET(bchannel, MUSB_HSDMA_CONTROL),
			0);
		musb_write_hsdma_addr(mbase, bchannel, 0);
		musb_write_hsdma_count(mbase, bchannel, 0);
		channel->status = MUSB_DMA_STATUS_FREE;

		if (musb_channel->transmit)
			musb_channel->controller->tx_active &= ~(1 << bchannel);
		else
			musb_channel->controller->rx_active &= ~(1 << bchannel);
	}

	return 0;
}

static irqreturn_t dma_controller_irq(int irq, void *private_data)
{
	struct musb_dma_controller *controller = private_data;
	struct musb *musb = controller->private_data;
	struct musb_dma_channel *mchannel;
	struct dma_channel *channel;

	void __iomem *mbase = controller->base;

	irqreturn_t retval = IRQ_NONE;

	unsigned long flags;

	u8 bchannel;
	u8 int_hsdma;

	u32 addr;
	u16 csr;

	spin_lock_irqsave(&musb->lock, flags);

	int_hsdma = musb_readb(mbase, MUSB_HSDMA_INTR);

#ifdef CONFIG_BLACKFIN
	/* Clear DMA interrupt flags */
	musb_writeb(mbase, MUSB_HSDMA_INTR, int_hsdma);
#endif

	if (!int_hsdma) {
		DBG(2, "spurious DMA irq\n");
		goto done;
	}

	for (bchannel = 0; bchannel < MUSB_HSDMA_CHANNELS; bchannel++) {
		u8 devctl;

		if (!(int_hsdma & (1 << bchannel)))
			continue;

		mchannel = &(controller->channel[bchannel]);
		channel = &mchannel->channel;

		csr = musb_readw(mbase, MUSB_HSDMA_CHANNEL_OFFSET(bchannel,
					MUSB_HSDMA_CONTROL));

		if (csr & MUSB_HSDMA_BUSERROR) {
			mchannel->channel.status = MUSB_DMA_STATUS_BUS_ABORT;
			goto done;
		}

		addr = musb_read_hsdma_addr(mbase, bchannel);
		channel->actual_len = addr - mchannel->start_addr;

		DBG(2, "ch %p, 0x%x -> 0x%x (%zu / %d) %s\n",
				channel, mchannel->start_addr,
				addr, channel->actual_len, mchannel->len,
				(channel->actual_len < mchannel->len) ?
				"=> reconfig 0" : "=> complete");

		/*
		 * Due to an ASIC bug, we need to manually clear PktRdy for
		 * receive channels. If more buffer space remains, just restart
		 * the DMA channel for the next packet and continue to the next
		 * channel. The address has automatically incremented in HW.
		 */
		if (!mchannel->transmit && musb->autoclear_wa) {
			u8  epnum  = mchannel->epnum;
			int offset = MUSB_EP_OFFSET(epnum, MUSB_RXCSR);
			u16 rxcsr;

			musb_ep_select(mbase, epnum);
			rxcsr = musb_readw(mbase, offset);
			rxcsr &= ~MUSB_RXCSR_RXPKTRDY;
			musb_writew(mbase, offset, rxcsr);

			if (channel->actual_len < mchannel->len) {
				int dmaoffset = MUSB_HSDMA_CHANNEL_OFFSET(
					mchannel->idx, MUSB_HSDMA_CONTROL);
				u16 dmacsr;

				musb_write_hsdma_count(mbase, mchannel->idx,
						mchannel->max_packet_sz);
				dmacsr = musb_readw(mbase, dmaoffset);
				dmacsr |= MUSB_HSDMA_ENABLE;
				musb_writew(mbase, dmaoffset, dmacsr);
				continue;
			}
		}

		devctl = musb_readb(mbase, MUSB_DEVCTL);
		channel->status = MUSB_DMA_STATUS_FREE;

		if (mchannel->transmit)
			controller->tx_active &= ~(1 << bchannel);
		else
			controller->rx_active &= ~(1 << bchannel);

		/* completed */
		if ((devctl & MUSB_DEVCTL_HM) && (mchannel->transmit)
				&& ((channel->desired_mode == 0)
					|| (channel->actual_len &
						(mchannel->max_packet_sz - 1)))) {
			u8  epnum  = mchannel->epnum;
			int offset = MUSB_EP_OFFSET(epnum, MUSB_TXCSR);
			u16 txcsr;
			/*
			 * The programming guide says that we
			 * must clear DMAENAB before DMAMODE.
			 */
			musb_ep_select(mbase, epnum);
			txcsr = musb_readw(mbase, offset);
			txcsr &= ~(MUSB_TXCSR_DMAENAB | MUSB_TXCSR_AUTOSET);
			musb_writew(mbase, offset, txcsr);
			/* Send out the packet */
			txcsr &= ~MUSB_TXCSR_DMAMODE;
			txcsr |=  MUSB_TXCSR_TXPKTRDY;
			musb_writew(mbase, offset, txcsr);
		} else {
			musb_dma_completion(musb, mchannel->epnum,
					mchannel->transmit);
		}
	}

	retval = IRQ_HANDLED;
done:
	spin_unlock_irqrestore(&musb->lock, flags);
	return retval;
}

void dma_controller_destroy(struct dma_controller *c)
{
	struct musb_dma_controller *controller = container_of(c,
			struct musb_dma_controller, controller);

	if (!controller)
		return;

	if (controller->irq)
		free_irq(controller->irq, c);

	kfree(controller);
}

struct dma_controller *__init
dma_controller_create(struct musb *musb, void __iomem *base)
{
	struct musb_dma_controller *controller;
	struct device *dev = musb->controller;
	struct platform_device *pdev = to_platform_device(dev);
	int irq = platform_get_irq(pdev, 1);

	if (irq == 0) {
		dev_err(dev, "No DMA interrupt line!\n");
		return NULL;
	}

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return NULL;

	controller->channel_count = MUSB_HSDMA_CHANNELS;
	controller->private_data = musb;
	controller->base = base;

	controller->controller.start = dma_controller_start;
	controller->controller.stop = dma_controller_stop;
	controller->controller.channel_alloc = dma_channel_allocate;
	controller->controller.channel_release = dma_channel_release;
	controller->controller.channel_program = dma_channel_program;
	controller->controller.channel_abort = dma_channel_abort;

	if (request_irq(irq, dma_controller_irq, IRQF_DISABLED,
			dev_name(musb->controller), &controller->controller)) {
		dev_err(dev, "request_irq %d failed!\n", irq);
		dma_controller_destroy(&controller->controller);

		return NULL;
	}

	controller->irq = irq;

	return &controller->controller;
}
