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
	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) }, /* Element thứ 3 quản lý chu kỳ ngủ */
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
	{ .srv = BT_MESH_ONOFF_SRV_INIT(&onoff_handlers) },
#endif
};

/* Định nghĩa macro ép trạng thái để code tường minh, dễ đọc và tránh nhầm lẫn */
#define LED_ON   false
#define LED_OFF    true

static void led_transition_start(struct led_ctx *led)
{
	int led_idx = led - &led_ctx[0];
	/* Khi có lệnh transition bật nguồn, gán trạng thái SÁNG chuẩn xác cho bo mạch */
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
 * QUẢN LÝ CHU KỲ NĂNG LƯỢNG QUA ELEMENT THỨ 3 (LED2)
 * ==================================================================== */
static struct k_work_delayable cycle_work;  /* Định thời chu kỳ 10s */
static struct k_work_delayable blink_work;  /* Định thời nhấp nháy nhanh LED2 */
static bool cycle_mode_active = false;      /* Mặc định ban đầu: KHÔNG bật chu kỳ ngủ */
static bool is_awake = true;                /* Trạng thái Thức/Ngủ trong chu kỳ */
static bool led2_blink_state = false;       /* Trạng thái toggle của LED2 */

/* Hàm xử lý nhấp nháy nhanh LED2 (Chỉ hoạt động khi chu kỳ kích hoạt và đang THỨC) */
static void blink_handler(struct k_work *work)
{
	if (!cycle_mode_active || !is_awake) {
		return; 
	}

	led2_blink_state = !led2_blink_state;
	
	/* Sử dụng toán tử điều kiện để nhấp nháy đúng trạng thái logic thực tế */
	dk_set_led(2, led2_blink_state ? LED_ON : LED_OFF);

	/* Nhấp nháy nhanh chu kỳ 150ms */
	k_work_reschedule(&blink_work, K_MSEC(100));
}

/* Hàm điều phối chu kỳ Thức 10s <-> Ngủ 10s */
static void cycle_handler(struct k_work *work)
{
	if (!cycle_mode_active) {
		return;
	}

	is_awake = !is_awake;

	if (is_awake) {
		/* ----------------------------------------------------
		 * 10 GIÂY THỨC: Khôi phục trạng thái Mesh và nhấp nháy LED2
		 * ---------------------------------------------------- */
		/* Khôi phục lại trạng thái bật/tắt đúng nghĩa của led0, led1, led3 */
		for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
			if (i != 2) { 
				dk_set_led(i, led_ctx[i].value ? LED_ON : LED_OFF);
			}
		}
		/* Kích hoạt lại tiến trình nhấp nháy nhanh cho LED2 */
		k_work_reschedule(&blink_work, K_NO_WAIT);

	} else {
		/* ----------------------------------------------------
		 * 10 GIÂY NGỦ: Ép tắt toàn bộ phần cứng để hạ dòng về 2-4uA
		 * ---------------------------------------------------- */
		k_work_cancel_delayable(&blink_work);

		/* ÉP TẮT HOÀN TOÀN TẤT CẢ ĐÈN ĐỂ ĐẢM BẢO KHÔNG DÒNG RÒ */
		for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
			dk_set_led(i, LED_OFF); 
		}
	}

	/* Tiếp tục gia hạn chu kỳ 10 giây tiếp theo */
	k_work_reschedule(&cycle_work, K_MSEC(10000));
}

/* Hàm tiếp nhận lệnh từ ứng dụng nRF Mesh */
static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    const struct bt_mesh_onoff_set *set,
		    struct bt_mesh_onoff_status *rsp)
{
	struct led_ctx *led = CONTAINER_OF(srv, struct led_ctx, srv);
	int led_idx = led - &led_ctx[0];

	/* ----------------------------------------------------------------
	 * TRƯỜNG HỢP 1: LỆNH ĐẾN TỪ ELEMENT THỨ 3 (led_idx == 2) -> ĐIỀU KHIỂN CHẾ ĐỘ CHU KỲ
	 * ---------------------------------------------------------------- */
	if (led_idx == 2) {
		led->value = set->on_off;

		if (set->on_off == 1) {
			/* Lệnh ON (1) từ App: Kích hoạt chu kỳ Thức 10s / Ngủ 10s */
			if (!cycle_mode_active) {
				cycle_mode_active = true;
				is_awake = true;
				k_work_reschedule(&cycle_work, K_MSEC(10000)); /* Bắt đầu 10s thức đầu tiên */
				k_work_reschedule(&blink_work, K_NO_WAIT);     /* Chạy nhấp nháy LED2 luôn */
			}
		} else {
			/* Lệnh OFF (0) từ App: Tắt chế độ chu kỳ, chỉ ở trạng thái Thức, LED2 sáng liên tục */
			cycle_mode_active = false;
			is_awake = true; /* Luôn thức */
			k_work_cancel_delayable(&cycle_work);
			k_work_cancel_delayable(&blink_work);
			
			/* Đảm bảo LED2 sáng liên tục khi tắt chế độ chu kỳ */
			dk_set_led(2, LED_ON);

			/* Khôi phục trạng thái thực tế chính xác của các LED còn lại theo cấu hình mạng Mesh */
			for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
				if (i != 2) {
					dk_set_led(i, led_ctx[i].value ? LED_ON : LED_OFF);
				}
			}
		}
	} 
	/* ----------------------------------------------------------------
	 * TRƯỜNG HỢP 2: LỆNH ĐẾN TỪ CÁC ELEMENT CÒN LẠI (0, 1, 3) -> CHẠY THEO NRF MESH
	 * ---------------------------------------------------------------- */
	else {
		/* Lưu trạng thái mong muốn (0 hoặc 1) từ nRF Mesh vào bộ đệm cấu hình */
		led->value = set->on_off; 

		if (!bt_mesh_model_transition_time(set->transition)) {
			led->remaining = 0;
			
			/* CHỈ cập nhật ra chân vật lý nếu mạch đang ở trạng thái THỨC */
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

/* Hiệu ứng chớp Identify lúc nạp Mesh đổi lại mức logic đồng bộ */
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
		/* dk_set_leds sử dụng bitmask hoạt động ngược với dk_set_led đơn lẻ */
		dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
		k_work_reschedule(&attention_blink_work, K_MSEC(30));
	} else {
		/* Ép tắt sạch đèn bằng cách cấu hình tắt đơn lẻ từng chân qua macro an toàn */
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

	/* ----------------------------------------------------
	 * MẶC ĐỊNH BAN ĐẦU: Chưa bật chu kỳ, tất cả LED đều OFF (trừ LED 2)
	 * ---------------------------------------------------- */
	cycle_mode_active = false;
	is_awake = true;
	
	for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
		dk_set_led(i, LED_ON); /* Đảm bảo tắt hoàn toàn 4 LED */
	}

	return &comp;
}