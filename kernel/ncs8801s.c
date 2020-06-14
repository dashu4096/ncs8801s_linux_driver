
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#if defined(CONFIG_OF)
#include <linux/of_gpio.h>
#endif

#include "ncs8801s.h"

struct ncs8801s *ncs8801s = NULL;

static int ncs8801s_read(struct i2c_client *i2c, char addr, char reg, char *val)
{
    int ret=-1;
    s32 retries = 0;
    struct i2c_msg msgs[2];

    msgs[0].flags = !I2C_M_RD;
    msgs[0].addr  = addr;
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;
    
    msgs[1].flags = I2C_M_RD;
    msgs[1].addr  = addr;
    msgs[1].len   = 1;
    msgs[1].buf   = val;

    while(retries < 3) {
		ret = i2c_transfer(i2c->adapter, msgs, 2);
		if(ret == 2)break;
		retries++;
	}
    return ret;
}

static int ncs8801s_write(struct i2c_client *i2c, char addr, char reg, char val)
{
	int ret=-1;
	struct i2c_msg msg;
	char tx_buf[2];

	if(!i2c)
		return ret;
	dev_dbg(&i2c->dev, "%s addr:%02x reg:%02x val:%02x\n",__func__,addr,reg,val);

	tx_buf[0] = reg;
	tx_buf[1] = val;

	msg.addr = addr;
	msg.buf = &tx_buf[0];
	msg.len = 2;
	msg.flags = i2c->flags;   

	ret = i2c_transfer(i2c->adapter, &msg, 1);
	return ret;	
}

static int ncs8801s_write_list(struct ncs8801s *ncs8801s, char addr, const struct reg_data *list)
{
	int len = 0;
	if (addr == NCS8801S_ID1_ADDR) {
		len = (ncs8801s->screen_w == 1920) ? ID1_1920_1080_REG_LEN : ID1_1366_768_REG_LEN;
	} else if (addr == NCS8801S_ID2_ADDR) {
		len = (ncs8801s->screen_w == 1920) ? ID2_1920_1080_REG_LEN : ID2_1366_768_REG_LEN;
	} else {
		len = ID3_1920_1080_REG_LEN;
	}

	while(len--) {
		if (ncs8801s_write(ncs8801s->i2c, addr, list->reg, list->val) < 0) {
			dev_err(&ncs8801s->i2c->dev, "ncs8801s write err addr:0x%02x reg:%02x,val:%02x\n",addr,list->reg,list->val);
		}
		list++;
	}
	return 0;
}

static int ncs8801s_init(void)
{
	if (ncs8801s != NULL) {
		int input_hactive = 0;
		char input_hactive_high = 0;
		char input_hactive_low = 0;
		ncs8801s_read(ncs8801s->i2c, NCS8801S_ID1_ADDR, 0xE4, &input_hactive_high);
		ncs8801s_read(ncs8801s->i2c, NCS8801S_ID1_ADDR, 0xE5, &input_hactive_low);
		input_hactive = input_hactive_low|(input_hactive_high<<8);
		dev_info(&ncs8801s->i2c->dev, "hactive 0x%x\n",input_hactive);
		if (input_hactive == 0) {
			//power init
			gpio_direction_output(ncs8801s->pwd_pin, 0);
			gpio_direction_output(ncs8801s->rst_pin, 0);
			usleep_range(60,80);
			gpio_set_value(ncs8801s->rst_pin, 1);
			usleep_range(60,80);

			if (ncs8801s->screen_w == 1920 && ncs8801s->screen_h == 1080) {
				//1920x1080
				ncs8801s_write_list(ncs8801s, NCS8801S_ID1_ADDR, id1_1920_1080_regs);
				ncs8801s_write_list(ncs8801s, NCS8801S_ID2_ADDR, id2_1920_1080_regs);
				ncs8801s_write(ncs8801s->i2c, NCS8801S_ID1_ADDR, 0x0f, 0x0);
				ncs8801s_write_list(ncs8801s, NCS8801S_ID3_ADDR, id3_1920_1080_regs);
				//B156HTN need
				ncs8801s_write(ncs8801s->i2c, NCS8801S_ID1_ADDR, 0x71, 0x9);
			} else {
				//1366x768
				ncs8801s_write_list(ncs8801s, NCS8801S_ID1_ADDR, id1_1366_768_regs);
				ncs8801s_write_list(ncs8801s, NCS8801S_ID2_ADDR, id2_1366_768_regs);
				ncs8801s_write(ncs8801s->i2c, NCS8801S_ID1_ADDR, 0x0f, 0x0);
			}
			dev_info(&ncs8801s->i2c->dev, "init %dx%d \n", ncs8801s->screen_w, ncs8801s->screen_h);
		}
	}
	return 0;
}

static void ncs8801s_parse_dt(struct ncs8801s *ncs8801s)
{
	int ret = 0;
	struct device_node *np = ncs8801s->i2c->dev.of_node;
	enum of_gpio_flags rst_flags;
	ncs8801s->rst_pin = of_get_named_gpio_flags(np, "rst_gpio", 0, &rst_flags);
	ncs8801s->pwd_pin = of_get_named_gpio_flags(np, "pwd_gpio", 0, &rst_flags);
	ret = gpio_request(ncs8801s->rst_pin, "ncs8801s_rst");
	if (ret != 0) {
		dev_err(&ncs8801s->i2c->dev, "%s: request ncs8801s rst error\n", __func__);
	}
	ret = gpio_request(ncs8801s->pwd_pin, "ncs8801s_pwd");
	if (ret != 0) {
		dev_err(&ncs8801s->i2c->dev, "%s: request ncs8801s pwd error\n", __func__);
	}
	ret = of_property_read_u32(np, "screen-w", &ncs8801s->screen_w);
	if (ret) {
		dev_err(&ncs8801s->i2c->dev, "no screen-w, use default\n");
		ncs8801s->screen_w = 1920;
	}
	ret = of_property_read_u32(np, "screen-h", &ncs8801s->screen_h);
	if (ret) {
		dev_err(&ncs8801s->i2c->dev, "no screen-w, use default\n");
		ncs8801s->screen_h = 1080;
	}
}

static int ncs8801s_i2c_probe(struct i2c_client *i2c,const struct i2c_device_id *id)
{
	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&i2c->dev, "Must have I2C_FUNC_I2C.\n");
		return -ENODEV;
	}
	dev_dbg(&i2c->dev, "addr %2x\n",i2c->addr);
	ncs8801s = devm_kzalloc(&i2c->dev, sizeof(struct ncs8801s), GFP_KERNEL);
	if (unlikely(!ncs8801s)) {
		dev_err(&i2c->dev, "alloc for struct ncs8801s fail\n");
		return -ENOMEM;
	}
	ncs8801s->i2c = i2c;
	ncs8801s_parse_dt(ncs8801s);
	i2c_set_clientdata(i2c, ncs8801s);
	ncs8801s_init();
	return 0;
}

static int  ncs8801s_i2c_remove(struct i2c_client *i2c)
{
	if (ncs8801s != NULL) {
		devm_kfree(&i2c->dev, ncs8801s);
	}
	return 0;
}

static const struct i2c_device_id id2_table[] = {
	{"newcosemi,ncs8801s", 0 },
	{ }
};

#if defined(CONFIG_OF)
static struct of_device_id ncs8801s_dt_ids[] = {
	{ .compatible = "newcosemi,ncs8801s" },
	{ }
};
#endif

static struct i2c_driver ncs8801s_i2c_driver  = {
	.driver = {
		.name  = "newcosemi,ncs8801s",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(ncs8801s_dt_ids),
#endif
	},
	.probe	  = &ncs8801s_i2c_probe,
	.remove   = &ncs8801s_i2c_remove,
	.id_table = id2_table,
};

static int __init ncs8801s_module_init(void)
{
	return i2c_add_driver(&ncs8801s_i2c_driver);
}

static void __exit ncs8801s_module_exit(void)
{
	i2c_del_driver(&ncs8801s_i2c_driver);
}

subsys_initcall(ncs8801s_module_init);
module_exit(ncs8801s_module_exit);

MODULE_AUTHOR("coder.hans@gmail.com");
MODULE_DESCRIPTION("NewCoSemi NCS8801S RGB/LVDS-to-eDP Converter Driver");
MODULE_LICENSE("GPL v2");
