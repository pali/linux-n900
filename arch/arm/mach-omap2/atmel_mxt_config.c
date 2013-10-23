#if defined(CONFIG_TOUCHSCREEN_ATMEL_MXT) || \
	defined(CONFIG_TOUCHSCREEN_ATMEL_MXT_MODULE)

#include <linux/kernel.h>
#include <linux/input/atmel_mxt.h>

#include "rm-680_himalaya_auo_v1_1.cfg.h"
#include "rm-696_pyrenees_smd_v1_6.cfg.h"

#include "atmel_mxt_config.h"

#define SETUP_HM(t) { t, mxt_auov11_t ## t, ARRAY_SIZE(mxt_auov11_t ## t) }

/* Order is important for crc calculation */
static const struct mxt_obj_config atmel_mxt_himalaya_obj_configs[] = {
	SETUP_HM(7),
	SETUP_HM(8),
	SETUP_HM(9),
	SETUP_HM(18),
	SETUP_HM(19),
	SETUP_HM(20),
	SETUP_HM(22),
	SETUP_HM(24),
	SETUP_HM(25),
	SETUP_HM(27),
	SETUP_HM(28),
};
#undef SETUP_HM

const struct mxt_config atmel_mxt_himalaya_config =  {
	.obj = atmel_mxt_himalaya_obj_configs,
	.num_objs = ARRAY_SIZE(atmel_mxt_himalaya_obj_configs),
};


#define SETUP_SMD(t) { t, mxt_smdv16_t ## t, ARRAY_SIZE(mxt_smdv16_t ## t) }

/* Order is important for crc calculation */

static const struct mxt_obj_config atmel_mxt_pyrenees_obj_configs[] = {
	SETUP_SMD(7),
	SETUP_SMD(8),
	SETUP_SMD(9),
	SETUP_SMD(15),
	SETUP_SMD(18),
	SETUP_SMD(19),
	SETUP_SMD(20),
	SETUP_SMD(22),
	SETUP_SMD(23),
	SETUP_SMD(24),
	SETUP_SMD(25),
	SETUP_SMD(27),
	SETUP_SMD(28),
};
#undef SETUP_SMD

const struct mxt_config atmel_mxt_pyrenees_config =  {
	.obj = atmel_mxt_pyrenees_obj_configs,
	.num_objs = ARRAY_SIZE(atmel_mxt_pyrenees_obj_configs),
};
#endif
