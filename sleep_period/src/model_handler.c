/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <bluetooth/mesh/models.h>
#include <dk_buttons_and_leds.h>
#include "model_handler.h"
#include <zephyr/logging/log.h>

// Thêm thư viện quản lý sâu tầng lõi Bluetooth Mesh của Nordic để điều khiển tắt/bật RF
#include <../subsys/bluetooth/mesh/net.h>
#include <../subsys/bluetooth/mesh/adv.h>

LOG_MODULE_REGISTER(model_handler, CONFIG_LOG_DEFAULT_LEVEL);

static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    const struct bt_mesh_onoff_set *set,
		    struct bt_mesh_onoff_status *rsp);

static void led_get(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    struct bt_mesh_onoff_status *rsp);

static const struct bt_mesh_onoff_srv_handlers onoff_handlers = {
	.set = led_set,
	.get = led_get,
};

struct led_ctx {
	struct bt_mesh_onoff_srv srv;
	struct k_work_delayable work;
	uint32_t remaining;
	bool value;
};

static struct led_ctx led_ctx[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led0))
	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) },
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) },
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) },
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) },
#endif
};

#define LED_ON   false
#define LED_OFF    true

static void led_transition_start(struct led_ctx *led)
{
	int led_idx = led - &led_ctx[0];
	dk_set_led(led_idx, LED_ON); 
	k_work_reschedule(&led->work, K_MSEC(led->remaining));
	led->remaining = 0;
}

static void led_status(struct led_ctx *led, struct bt_mesh_onoff_status *status)
{
	status->remaining_time = led->remaining ? led->remaining :
		k_ticks_to_ms_ceil32(k_work_delayable_remaining_get(&led->work));
	status->target_on_off = led->value;
	status->present_on_off = led->value || status->remaining_time;
}

/* ====================================================================
 * LOGIC QUẢN LÝ CHU KỲ NĂNG LƯỢNG TIẾT KIỆM NGUỒN SÂU
 * ==================================================================== */
static struct k_work_delayable cycle_work;  
static struct k_work_delayable blink_work;  
static bool cycle_mode_active = false;      
static bool is_awake = true;                
static bool led2_blink_state = false;       

static void blink_handler(struct k_work *work)
{
	if (!cycle_mode_active || !is_awake) {
		return; 
	}

	led2_blink_state = !led2_blink_state;
	dk_set_led(2, led2_blink_state ? LED_ON : LED_OFF);
	k_work_reschedule(&blink_work, K_MSEC(100));
}

static void cycle_handler(struct k_work *work)
{
	if (!cycle_mode_active) {
		return;
	}

	is_awake = !is_awake;

	if (is_awake) {
		/* ----------------------------------------------------
		 * THỨC GIẤC: Bật lại RF Mesh trước rồi mới bật LED
		 * ---------------------------------------------------- */
		bt_mesh_resume();

		for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
			if (i != 2) { 
				dk_set_led(i, led_ctx[i].value ? LED_ON : LED_OFF);
			}
		}
		k_work_reschedule(&blink_work, K_NO_WAIT);

	} else {
		/* ----------------------------------------------------
		 * ĐI NGỦ: TẮT LED TRƯỚC -> CHO MESH NGỦ SAU
		 * ---------------------------------------------------- */
		k_work_cancel_delayable(&blink_work);

		// Bước 1: Ép tắt toàn bộ LED vật lý để giải phóng chân GPIO hoàn toàn
		for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
			dk_set_led(i, LED_OFF); 
		}

		// Bước 2: Sau khi mọi ngoại vi đã tắt, mới cho phép Mesh đóng Radio ngủ đông.
		// Lúc này không còn lệnh nào can thiệp chân nữa, hệ thống sẽ rơi vào trạng thái ngủ sâu tuyệt đối.
		bt_mesh_suspend();
	}

	k_work_reschedule(&cycle_work, K_MSEC(10000));
}

static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    const struct bt_mesh_onoff_set *set,
		    struct bt_mesh_onoff_status *rsp)
{
	struct led_ctx *led = CONTAINER_OF(srv, struct led_ctx, srv);
	int led_idx = led - &led_ctx[0];

	if (led_idx == 2) {
		led->value = set->on_off;

		if (set->on_off == 1) {
			if (!cycle_mode_active) {
				cycle_mode_active = true;
				is_awake = true;
				k_work_reschedule(&cycle_work, K_MSEC(10000)); 
				k_work_reschedule(&blink_work, K_NO_WAIT);     
			}
		} else {
			cycle_mode_active = false;
			is_awake = true; 
			k_work_cancel_delayable(&cycle_work);
			k_work_cancel_delayable(&blink_work);
			
			// Đảm bảo đánh thức lại bộ phát nếu tắt chu kỳ ngủ giữa chừng
			bt_mesh_resume();

			dk_set_led(2, LED_ON);

			for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
				if (i != 2) {
					dk_set_led(i, led_ctx[i].value ? LED_ON : LED_OFF);
				}
			}
		}
	} 
	else {
		led->value = set->on_off; 

		if (!bt_mesh_model_transition_time(set->transition)) {
			led->remaining = 0;
			if (is_awake) {
				dk_set_led(led_idx, set->on_off ? LED_ON : LED_OFF);
			}
		} else {
			led->remaining = set->transition->time;
			if (set->transition->delay) {
				k_work_reschedule(&led->work, K_MSEC(set->transition->delay));
			} else {
				if (is_awake) {
					led_transition_start(led);
				}
			}
		}
	}

	if (rsp) {
		led_status(led, rsp);
	}
}

static void led_get(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    struct bt_mesh_onoff_status *rsp)
{
	struct led_ctx *led = CONTAINER_OF(srv, struct led_ctx, srv);
	led_status(led, rsp);
}

static void led_work(struct k_work *work)
{
	struct led_ctx *led = CONTAINER_OF(work, struct led_ctx, work.work);
	int led_idx = led - &led_ctx[0];

	if (led->remaining) {
		led_transition_start(led);
	} else {
		if (is_awake && led_idx != 2) {
			dk_set_led(led_idx, led->value ? LED_ON : LED_OFF);
		}

		struct bt_mesh_onoff_status status;
		led_status(led, &status);
		bt_mesh_onoff_srv_pub(&led->srv, NULL, &status);
	}
}

static struct k_work_delayable attention_blink_work;
static bool attention;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led0))
		BIT(0),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
		BIT(1),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
		BIT(2),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
		BIT(3),
#endif
	};

	if (attention) {
		dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
		k_work_reschedule(&attention_blink_work, K_MSEC(30));
	} else {
		for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
			dk_set_led(i, LED_OFF);
		}
	}
}

static void attention_on(const struct bt_mesh_model *mod)
{
	attention = true;
	k_work_reschedule(&attention_blink_work, K_NO_WAIT);
}

static void attention_off(const struct bt_mesh_model *mod)
{
	attention = false;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

static struct bt_mesh_elem elements[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led0))
	BT_MESH_ELEM(
		1, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_CFG_SRV,
			BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
			BT_MESH_MODEL_ONOFF_SRV(&led_ctx[0].srv)),
		BT_MESH_MODEL_NONE),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
	BT_MESH_ELEM(
		2, BT_MESH_MODEL_LIST(BT_MESH_MODEL_ONOFF_SRV(&led_ctx[1].srv)),
		BT_MESH_MODEL_NONE),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
	BT_MESH_ELEM(
		3, BT_MESH_MODEL_LIST(BT_MESH_MODEL_ONOFF_SRV(&led_ctx[2].srv)),
		BT_MESH_MODEL_NONE),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
	BT_MESH_ELEM(
		4, BT_MESH_MODEL_LIST(BT_MESH_MODEL_ONOFF_SRV(&led_ctx[3].srv)),
		BT_MESH_MODEL_NONE),
#endif
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

const struct bt_mesh_comp *model_handler_init(void)
{
	k_work_init_delayable(&attention_blink_work, attention_blink);

	for (int i = 0; i < ARRAY_SIZE(led_ctx); ++i) {
		k_work_init_delayable(&led_ctx[i].work, led_work);
		led_ctx[i].value = 0; 
	}

	k_work_init_delayable(&blink_work, blink_handler);
	k_work_init_delayable(&cycle_work, cycle_handler);

	cycle_mode_active = false;
	is_awake = true;
	
	for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
		dk_set_led(i, LED_OFF); 
	}
	dk_set_led(2, LED_ON);

	return &comp;
}