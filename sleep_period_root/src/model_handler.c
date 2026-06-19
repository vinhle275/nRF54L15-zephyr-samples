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
#include <zephyr/net/buf.h> /* THÊM: Thư viện xử lý buffer cho Vendor Model */

LOG_MODULE_REGISTER(model_handler, CONFIG_LOG_DEFAULT_LEVEL);

/* ========================================================================= */
/* --- PHẦN THÊM MỚI: ĐỊNH NGHĨA VÀ XỬ LÝ VENDOR MODEL NHẬN SỐ NGUYÊN 32-BIT --- */
#define VND_COMPANY_ID   0x0059   // Mã công ty Nordic
#define VND_MODEL_ID_SRV 0x0002   // ID Model Nhận
#define VND_OP_SENSOR_DATA BT_MESH_MODEL_OP_3(0x01, VND_COMPANY_ID)

#define MAX_SENSORS 10
struct sensor_node_t {
    uint16_t addr;
    uint32_t value; 
    bool is_active;
};
static struct sensor_node_t sensor_list[MAX_SENSORS];

/* Hàm in danh sách ra Segger RTT */
static void update_and_print_sensor_list(uint16_t sender_addr, uint32_t sensor_value)
{
    bool found = false;
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (sensor_list[i].is_active && sensor_list[i].addr == sender_addr) {
            sensor_list[i].value = sensor_value;
            found = true;
            break;
        }
    }
    if (!found) {
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (!sensor_list[i].is_active) {
                sensor_list[i].addr = sender_addr;
                sensor_list[i].value = sensor_value;
                sensor_list[i].is_active = true;
                break;
            }
        }
    }

    LOG_INF("\n========================================");
    LOG_INF("      DANH SACH DU LIEU SENSOR HUB      ");
    LOG_INF("========================================");
    LOG_INF("|   DIA CHI NODE    |  GIA TRI SENSOR  |");
    LOG_INF("----------------------------------------");
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (sensor_list[i].is_active) {
            LOG_INF("|      0x%04X       |    %10u    |", 
                    sensor_list[i].addr, sensor_list[i].value);
        }
    }
    LOG_INF("========================================\n");
}

/* Callback xử lý khi có gói tin Vendor bay tới */
static int handle_sensor_data(const struct bt_mesh_model *model,
			      struct bt_mesh_msg_ctx *ctx,
			      struct net_buf_simple *buf)
{
	uint32_t sensor_value = net_buf_simple_pull_le32(buf); // Lấy số 32-bit từ payload
	update_and_print_sensor_list(ctx->addr, sensor_value);
	return 0;
}

static const struct bt_mesh_model_op vnd_srv_ops[] = {
	{ VND_OP_SENSOR_DATA, 4, handle_sensor_data }, 
	BT_MESH_MODEL_OP_END,
};
/* ========================================================================= */

/* --- TỪ ĐÂY TRỞ XUỐNG LÀ LOGIC GỐC CỦA BẠN (ĐƯỢC GIỮ NGUYÊN) --- */

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

static void led_transition_start(struct led_ctx *led)
{
	int led_idx = led - &led_ctx[0];
	dk_set_led(led_idx, true);
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

static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    const struct bt_mesh_onoff_set *set,
		    struct bt_mesh_onoff_status *rsp)
{
	LOG_INF("Tin nhan nhan duoc: %d, tu source: 0x%04x", set->on_off, ctx->addr);
	struct led_ctx *led = CONTAINER_OF(srv, struct led_ctx, srv);
	int led_idx = led - &led_ctx[0];

	if (set->on_off == led->value) {
		goto respond;
	}

	led->value = set->on_off;
	if (!bt_mesh_model_transition_time(set->transition)) {
		led->remaining = 0;
		dk_set_led(led_idx, set->on_off);
		goto respond;
	}

	led->remaining = set->transition->time;

	if (set->transition->delay) {
		k_work_reschedule(&led->work, K_MSEC(set->transition->delay));
	} else {
		led_transition_start(led);
	}

respond:
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
		dk_set_led(led_idx, led->value);
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
		dk_set_leds(DK_NO_LEDS_MSK);
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
		/* THÊM MỚI VÀO ELEMENT 1: Thêm cái Vendor Server Model vào đuôi */
		BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_VND(VND_COMPANY_ID, VND_MODEL_ID_SRV, vnd_srv_ops, NULL, NULL)
		)),
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
	}

	return &comp;
}