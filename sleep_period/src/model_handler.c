// /*
//  * Copyright (c) 2019 Nordic Semiconductor ASA
//  *
//  * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
//  */

// #include <zephyr/bluetooth/bluetooth.h>
// #include <bluetooth/mesh/models.h>
// #include <dk_buttons_and_leds.h>
// #include "model_handler.h"
// #include <zephyr/logging/log.h>
// LOG_MODULE_REGISTER(model_handler, CONFIG_LOG_DEFAULT_LEVEL);

// static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
// 		    const struct bt_mesh_onoff_set *set,
// 		    struct bt_mesh_onoff_status *rsp);

// static void led_get(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
// 		    struct bt_mesh_onoff_status *rsp);

// static const struct bt_mesh_onoff_srv_handlers onoff_handlers = {
// 	.set = led_set,
// 	.get = led_get,
// };

// struct led_ctx {
// 	struct bt_mesh_onoff_srv srv;
// 	struct k_work_delayable work;
// 	uint32_t remaining;
// 	bool value;
// };

// static struct led_ctx led_ctx[] = {
// #if DT_NODE_EXISTS(DT_ALIAS(led0))
// 	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) },
// #endif
// #if DT_NODE_EXISTS(DT_ALIAS(led1))
// 	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) },
// #endif
// #if DT_NODE_EXISTS(DT_ALIAS(led2))
// 	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) },
// #endif
// #if DT_NODE_EXISTS(DT_ALIAS(led3))
// 	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) },
// #endif
// };

// static void led_transition_start(struct led_ctx *led)
// {
// 	int led_idx = led - &led_ctx[0];

// 	/* As long as the transition is in progress, the onoff
// 	 * state is "on":
// 	 */
// 	dk_set_led(led_idx, true);
// 	k_work_reschedule(&led->work, K_MSEC(led->remaining));
// 	led->remaining = 0;
// }

// static void led_status(struct led_ctx *led, struct bt_mesh_onoff_status *status)
// {
// 	/* Do not include delay in the remaining time. */
// 	status->remaining_time = led->remaining ? led->remaining :
// 		k_ticks_to_ms_ceil32(k_work_delayable_remaining_get(&led->work));
// 	status->target_on_off = led->value;
// 	/* As long as the transition is in progress, the onoff state is "on": */
// 	status->present_on_off = led->value || status->remaining_time;
// }

// static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
// 		    const struct bt_mesh_onoff_set *set,
// 		    struct bt_mesh_onoff_status *rsp)
// {
// 	LOG_INF("Tin nhan nhan duoc: %d, tu source: 0x%04x", set->on_off, ctx->addr);
// 	struct led_ctx *led = CONTAINER_OF(srv, struct led_ctx, srv);
// 	int led_idx = led - &led_ctx[0];

// 	if (set->on_off == led->value) {
// 		goto respond;
// 	}

// 	led->value = set->on_off;
// 	if (!bt_mesh_model_transition_time(set->transition)) {
// 		led->remaining = 0;
// 		dk_set_led(led_idx, set->on_off);
// 		goto respond;
// 	}

// 	led->remaining = set->transition->time;

// 	if (set->transition->delay) {
// 		k_work_reschedule(&led->work, K_MSEC(set->transition->delay));
// 	} else {
// 		led_transition_start(led);
// 	}

// respond:
// 	if (rsp) {
// 		led_status(led, rsp);
// 	}
// }

// static void led_get(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
// 		    struct bt_mesh_onoff_status *rsp)
// {
// 	struct led_ctx *led = CONTAINER_OF(srv, struct led_ctx, srv);

// 	led_status(led, rsp);
// }

// static void led_work(struct k_work *work)
// {
// 	struct led_ctx *led = CONTAINER_OF(work, struct led_ctx, work.work);
// 	int led_idx = led - &led_ctx[0];

// 	if (led->remaining) {
// 		led_transition_start(led);
// 	} else {
// 		dk_set_led(led_idx, led->value);

// 		/* Publish the new value at the end of the transition */
// 		struct bt_mesh_onoff_status status;

// 		led_status(led, &status);
// 		bt_mesh_onoff_srv_pub(&led->srv, NULL, &status);
// 	}
// }

// /* Set up a repeating delayed work to blink the DK's LEDs when attention is
//  * requested.
//  */
// static struct k_work_delayable attention_blink_work;
// static bool attention;

// static void attention_blink(struct k_work *work)
// {
// 	static int idx;
// 	const uint8_t pattern[] = {
// #if DT_NODE_EXISTS(DT_ALIAS(led0))
// 		BIT(0),
// #endif
// #if DT_NODE_EXISTS(DT_ALIAS(led1))
// 		BIT(1),
// #endif
// #if DT_NODE_EXISTS(DT_ALIAS(led2))
// 		BIT(2),
// #endif
// #if DT_NODE_EXISTS(DT_ALIAS(led3))
// 		BIT(3),
// #endif
// 	};

// 	if (attention) {
// 		dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
// 		k_work_reschedule(&attention_blink_work, K_MSEC(30));
// 	} else {
// 		dk_set_led(0, true); // Tắt LED 1 (index 0)
// 		dk_set_led(1, true); // Tắt LED 2 (index 1)
// 		dk_set_led(2, true); // Tắt LED 3 (index 2)
// 		dk_set_led(3, true); // Tắt LED 4 (index 3)
// 	}
// }

// static void attention_on(const struct bt_mesh_model *mod)
// {
// 	attention = true;
// 	k_work_reschedule(&attention_blink_work, K_NO_WAIT);
// }

// static void attention_off(const struct bt_mesh_model *mod)
// {
// 	/* Will stop rescheduling blink timer */
// 	attention = false;
// }

// static const struct bt_mesh_health_srv_cb health_srv_cb = {
// 	.attn_on = attention_on,
// 	.attn_off = attention_off,
// };

// static struct bt_mesh_health_srv health_srv = {
// 	.cb = &health_srv_cb,
// };

// BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

// static struct bt_mesh_elem elements[] = {
// #if DT_NODE_EXISTS(DT_ALIAS(led0))
// 	BT_MESH_ELEM(
// 		1, BT_MESH_MODEL_LIST(
// 			BT_MESH_MODEL_CFG_SRV,
// 			BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
// 			BT_MESH_MODEL_ONOFF_SRV(&led_ctx[0].srv)),
// 		BT_MESH_MODEL_NONE),
// #endif
// #if DT_NODE_EXISTS(DT_ALIAS(led1))
// 	BT_MESH_ELEM(
// 		2, BT_MESH_MODEL_LIST(BT_MESH_MODEL_ONOFF_SRV(&led_ctx[1].srv)),
// 		BT_MESH_MODEL_NONE),
// #endif
// #if DT_NODE_EXISTS(DT_ALIAS(led2))
// 	BT_MESH_ELEM(
// 		3, BT_MESH_MODEL_LIST(BT_MESH_MODEL_ONOFF_SRV(&led_ctx[2].srv)),
// 		BT_MESH_MODEL_NONE),
// #endif
// #if DT_NODE_EXISTS(DT_ALIAS(led3))
// 	BT_MESH_ELEM(
// 		4, BT_MESH_MODEL_LIST(BT_MESH_MODEL_ONOFF_SRV(&led_ctx[3].srv)),
// 		BT_MESH_MODEL_NONE),
// #endif
// };

// static const struct bt_mesh_comp comp = {
// 	.cid = CONFIG_BT_COMPANY_ID,
// 	.elem = elements,
// 	.elem_count = ARRAY_SIZE(elements),
// };




// static struct k_work_delayable cycle_work;  /* Định nghĩa công việc quản lý chu kỳ 10s */
// static struct k_work_delayable blink_work;  /* Định nghĩa công việc nhấp nháy LED2 */
// static bool is_awake = true;                /* Trạng thái ban đầu: Thức */
// static bool led2_state = false;             /* Trạng thái chớp/tắt của LED2 */

// /* Hàm xử lý nhấp nháy LED2 trong 10s thức */
// static void blink_handler(struct k_work *work)
// {
// 	if (!is_awake) {
// 		return; /* Nếu đang ngủ thì không chạy tiếp */
// 	}

// 	/* Đảo trạng thái và cập nhật LED2 */
// 	led2_state = !led2_state;
// 	dk_set_led(2, led2_state); /* LED thứ 3 có index là 2 */

// 	/* Tiếp tục lên lịch nhấp nháy sau mỗi 500ms (có thể chỉnh tùy ý) */
// 	k_work_reschedule(&blink_work, K_MSEC(500));
// }

// /* Hàm điều phối chu kỳ Thức 10s -> Ngủ 10s */
// static void cycle_handler(struct k_work *work)
// {
// 	/* Đảo trạng thái hệ thống */
// 	is_awake = !is_awake;

// 	if (is_awake) {
// 		LOG_INF("--- MACH THUC (10 giay) ---");
		
// 		/* 1. Tắt các LED còn lại để đảm bảo yêu cầu */
// 		dk_set_led(0, true);
// 		dk_set_led(1, true);
// 		dk_set_led(3, true);

// 		/* 2. Bắt đầu kích hoạt nhấp nháy LED2 */
// 		led2_state = true;
// 		dk_set_led(2, led2_state);
// 		k_work_reschedule(&blink_work, K_MSEC(500));

// 	} else {
// 		LOG_INF("--- MACH NGU (10 giay) - TAT CAM BIEN/LED ---");
		
// 		/* Dừng việc nhấp nháy */
// 		k_work_cancel_delayable(&blink_work);

// 		/* Tắt toàn bộ 4 LED */
// 		dk_set_led(0, true); // Tắt LED 1 (index 0)
// 		dk_set_led(1, true); // Tắt LED 2 (index 1)
// 		dk_set_led(2, true); // Tắt LED 3 (index 2)
// 		dk_set_led(3, true); // Tắt LED 4 (index 3)
// 	}

// 	/* Tiếp tục lặp lại chu kỳ sau 10 giây (10000 ms) */
// 	k_work_reschedule(&cycle_work, K_MSEC(10000));
// }



// const struct bt_mesh_comp *model_handler_init(void)
// {
// 	k_work_init_delayable(&attention_blink_work, attention_blink);

// 	for (int i = 0; i < ARRAY_SIZE(led_ctx); ++i) {
// 		k_work_init_delayable(&led_ctx[i].work, led_work);
// 	}

// 	/* Khởi tạo các công việc (work) mới được thêm vào */
// 	k_work_init_delayable(&blink_work, blink_handler);
// 	k_work_init_delayable(&cycle_work, cycle_handler);

// 	/* Bắt đầu chu kỳ đầu tiên ngay lập tức (hoặc sau 10s thức đầu tiên) */
// 	/* Ở đây mạch sẽ bắt đầu ở trạng thái THỨC trong 10s đầu tiên */
// 	dk_set_led(0, false);
// 	dk_set_led(1, false);
// 	dk_set_led(3, false);
// 	k_work_reschedule(&blink_work, K_MSEC(500));
	
// 	/* Lên lịch để chuyển sang trạng thái NGỦ sau 10 giây */
// 	k_work_reschedule(&cycle_work, K_MSEC(10000));

// 	return &comp;
// }