#include <linux/kernel.h>
#include <linux/device.h>
#include <media/mv_sc2_twsi_conf.h>

static int twsi3_owner;
static DEFINE_MUTEX(sc2_mutex);

int sc2_select_pins_state(struct device *dev,
		int pin_state, int sc2_mod)
{
	struct pinctrl *pin;
	int ret = 0;

	if (!dev)
		return -EINVAL;

	mutex_lock(&sc2_mutex);
	if (twsi3_owner && twsi3_owner != sc2_mod) {
		dev_err(dev, "pin is occupied by other module\n");
		ret = -EBUSY;
		goto out;
	}

	if (pin_state == SC2_PIN_ST_TWSI)
		pin = pinctrl_get_select(dev, "twsi3");
	else
		pin = pinctrl_get_select(dev, PINCTRL_STATE_DEFAULT);

	ret = IS_ERR(pin);
	if (ret < 0) {
		dev_err(dev, "could not configure pins");
		goto out;
	}

	if (pin_state == SC2_PIN_ST_TWSI)
		twsi3_owner = sc2_mod;
	else
		twsi3_owner = 0;

out:
	mutex_unlock(&sc2_mutex);
	return ret;
}
