/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h> 
#include <bluetooth/mesh/models.h>
#include <dk_buttons_and_leds.h>
#include "model_handler.h"
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h> /* THÊM: Thư viện xử lý buffer cho Vendor Model */

LOG_MODULE_REGISTER(model_handler, CONFIG_LOG_DEFAULT_LEVEL);

/* ========================================================================= */
/* --- PHẦN THÊM MỚI: ĐỊNH NGHĨA VÀ XỬ LÝ VENDOR MODEL GỬI SỐ NGUYÊN 32-BIT --- */
#define VND_COMPANY_ID   0x0059   // Mã công ty Nordic
#define VND_MODEL_ID_CLI 0x0001   // ID Model Gửi cho mạch Sensor
#define VND_OP_SENSOR_DATA BT_MESH_MODEL_OP_3(0x01, VND_COMPANY_ID)

BT_MESH_MODEL_PUB_DEFINE(vnd_pub, NULL, 15);
static struct bt_mesh_model *vnd_cli_model;

static const struct bt_mesh_model_op vnd_cli_ops[] = {
	BT_MESH_MODEL_OP_END,
};

/* HÀM MỚI: Dùng để phát số nguyên lớn 32-bit thay vì chỉ bắn On/Off */
static void send_mesh_sensor_data_32bit(uint32_t sensor_value)
{
	if (!vnd_cli_model || vnd_cli_model->pub->addr == BT_MESH_ADDR_UNASSIGNED) {
		LOG_WRN("--- [PHAT SONG]: Chua cau hinh Publish Address cho Vendor Model tren App! ---");
		return;
	}

	struct net_buf_simple *msg = vnd_cli_model->pub->msg;
	net_buf_simple_reset(msg);
	
	bt_mesh_model_msg_init(msg, VND_OP_SENSOR_DATA);
	net_buf_simple_add_le32(msg, sensor_value); // Ép số nguyên 32-bit vào gói tin

	int err = bt_mesh_model_publish(vnd_cli_model);
	if (err) {
		LOG_ERR("Loi gui tin Vendor (err %d)", err);
	} else {
		LOG_INF("--- [PHAT SONG VENDOR]: Da gui du lieu Sensor = %u den Target [0x%04X] ---", 
				sensor_value, vnd_cli_model->pub->addr);
	}
}
/* ========================================================================= */

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
#define LED_OFF  true

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
 * LOGIC CHU KỲ NGUỒN VÀ TRUYỀN DỮ LIỆU CÓ BỘ ĐỆM AN TOÀN
 * ==================================================================== */
static struct k_work_delayable cycle_work;   // Timer đảo trạng thái Thức/Ngủ
static struct k_work_delayable suspend_work; // Timer delay để tắt Radio an toàn
static struct k_work_delayable blink_work;   // Timer nháy LED 2
static struct k_work_delayable publish_work; // Timer gửi gói tin định kỳ

static bool cycle_mode_active = false;      
static bool is_awake = true;                
static bool led2_blink_state = false;       
static bool next_state_on = true; 

static struct bt_mesh_onoff_cli onoff_cli; 

/* ----- THÊM MỚI: BIẾN VÀ HÀM MÔ PHỎNG SENSOR ----- */
static uint32_t simulated_sensor_value = 0;

static void display_sensor_value(void)
{
	uint8_t mod_val = simulated_sensor_value % 4;

	bool bit0 = (mod_val & 0x01) != 0; // Trạng thái cho LED1 (Index 0)
	bool bit1 = (mod_val & 0x02) != 0; // Trạng thái cho LED2 (Index 1)

	dk_set_led(0, bit0 ? LED_ON : LED_OFF);
	dk_set_led(1, bit1 ? LED_ON : LED_OFF);
}
/* ------------------------------------------------- */

/* HÀM PHỤ TRỢ: Đóng gói và gửi lệnh Mesh tới Group C002 */
static void send_mesh_packet(bool turn_on)
{
	/* Kiểm tra xem Model đã được cấu hình địa chỉ Publish trên nRF Mesh App chưa */
	if (onoff_cli.pub.addr == BT_MESH_ADDR_UNASSIGNED) {
		LOG_WRN("--- [PHAT SONG]: Chua cau hinh Publish Address tren nRF Mesh App! ---");
		return;
	}

	struct bt_mesh_onoff_set set = {
		.on_off = turn_on ? 1 : 0, 
		.transition = NULL,
	};

	/* LẤY ĐỊA CHỈ & APP KEY ĐỘNG: Trích xuất trực tiếp từ cấu hình nRF Mesh */
	struct bt_mesh_msg_ctx ctx = {
		.app_idx = onoff_cli.pub.key,       // Tự động map theo AppKey được gán khi Publish
		.addr = onoff_cli.pub.addr,         // Gửi tới mạch cụ thể/Group cấu hình trên App
		.send_ttl = BT_MESH_TTL_DEFAULT,              
	};

	int err = bt_mesh_onoff_cli_set(&onoff_cli, &ctx, &set, NULL);
	
	if (err && err != -EALREADY) {
		LOG_ERR("Loi gui tin (err %d)", err);
	} else {
		LOG_INF("--- [PHAT SONG]: Da gui lenh %s den Target [0x%04X] ---", 
				turn_on ? "BAT (ON)" : "TAT (OFF)", onoff_cli.pub.addr);
	}
}

/* HÀM XỬ LÝ NHÁY LED 2: Chỉ nháy khi thức */
static void blink_handler(struct k_work *work)
{
	if (!cycle_mode_active || !is_awake) {
		return; // Không nháy nếu đang đi ngủ
	}

	led2_blink_state = !led2_blink_state;
	dk_set_led(2, led2_blink_state ? LED_ON : LED_OFF);
	
	k_work_reschedule(&blink_work, K_MSEC(100));
}

/* HÀM BẮN GÓI TIN: THAY ĐỔI ĐỂ PHÁT SỐ NGUYÊN LỚN THAY VÌ ON/OFF CHU KỲ */
static void publish_handler(struct k_work *work)
{
	if (!cycle_mode_active || !is_awake) {
		return;
	}

	/* THAY ĐỔI: Thay vì gọi lệnh On/Off cũ, ta gọi hàm phát số nguyên lớn thực tế */
	// send_mesh_packet(next_state_on); // Logic cũ (comment lại)
	// next_state_on = !next_state_on;   // Logic cũ (comment lại)

	/* Gửi giá trị số nguyên lớn mô phỏng hiện tại của sensor lên Hub */
	send_mesh_sensor_data_32bit(simulated_sensor_value);

	k_work_reschedule(&publish_work, K_MSEC(1000));
}

/* HÀM MỚI: Thực hiện tắt Radio (Ngủ Sâu) sau khi gói tin đã bay đi an toàn */
static void suspend_handler(struct k_work *work)
{
	// Bảo vệ 2 lớp: Nếu chu kỳ bị ngắt giữa chừng hoặc mạch đang thức thì không tắt Radio
	if (is_awake || !cycle_mode_active) {
		return;
	}

	k_work_cancel_delayable(&blink_work);  

	/* ÉP TẮT TOÀN BỘ LED TRÊN MẠCH KHI ĐI NGỦ */
	for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
		dk_set_led(i, LED_OFF);
	}

	// ĐÓNG BĂNG MẠNG: Ngắt hoàn toàn chip RF để hạ dòng xuống mức uA
	bt_mesh_suspend(); 
	LOG_INF("--- [RADIO]: Da dong bang Radio, TAT HET LED, ngat mach di ngu! ---");
}
/* HÀM QUẢN LÝ CHU KỲ NGUỒN: Chỉ điều phối, nhường việc ngủ cho suspend_work */
static void cycle_handler(struct k_work *work)
{
	if (!cycle_mode_active) {
		return;
	}

	is_awake = !is_awake;

	if (is_awake) {
		/* --- 🟢 MẠCH THỨC GIẤC (10s) --- */
		bt_mesh_resume(); // Đánh thức Radio ngay lập tức
		LOG_INF("--- [RADIO]: Da khoi dong lai Radio! ---");

		for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
			if (i != 1 && i != 2 && i != 3) { 
				dk_set_led(i, led_ctx[i].value ? LED_ON : LED_OFF);
			}
		}
		
		led2_blink_state = true;
		dk_set_led(2, LED_ON);
		k_work_reschedule(&blink_work, K_MSEC(100)); // LED nháy ngay lập tức

		next_state_on = true; 
		
		// ĐỢI 500MS SAU KHI THỨC: Cho chip RF nạp đủ năng lượng và kết nối mạng rồi mới bắn tin
		k_work_reschedule(&publish_work, K_MSEC(500)); 

		// Hẹn giờ đi ngủ sau 10s
		k_work_reschedule(&cycle_work, K_MSEC(10000));

		/* --- CHỈ TĂNG VÀ HIỂN THỊ SENSOR MỖI KHI BẮT ĐẦU CHU KỲ THỨC MỚI --- */
		simulated_sensor_value += 1; /* THAY ĐỔI: Cộng bước lớn (ví dụ +1235) để tạo ra số nguyên lớn thực sự */
		LOG_INF("--- [SENSOR]: Gia tri sensor hien tai = %d ---", simulated_sensor_value);
		display_sensor_value();

	} else {
		/* --- 🔴 MẠCH SẮP ĐI NGỦ (10s) --- */
		
		k_work_cancel_delayable(&publish_work); // Ngừng gửi tin chu kỳ 1s

		// ÂN HẠN 1 GIÂY (1000ms): Chờ gói tin bay đi trót lọt rồi mới ủy quyền cho suspend_handler rút điện Radio!
		k_work_reschedule(&suspend_work, K_NO_WAIT);

		// Hẹn giờ thức giấc sau 10s
		k_work_reschedule(&cycle_work, K_MSEC(10000));
	}
}

static void led_set(struct bt_mesh_onoff_srv *srv, struct bt_mesh_msg_ctx *ctx,
		    const struct bt_mesh_onoff_set *set,
		    struct bt_mesh_onoff_status *rsp)
{
	struct led_ctx *led = CONTAINER_OF(srv, struct led_ctx, srv);
	int led_idx = led - &led_ctx[0];

	/* ----- ĐÓN VÀ XỬ LÝ GÓI TIN MẠCH B (LED 1) ----- */
	if (led_idx == 1) { 
		led->value = set->on_off;
		
		if (set->on_off == 1) {
			dk_set_led(1, LED_ON);
		} else {
			dk_set_led(1, LED_OFF);
		}
	} 
	/* ----- KÍCH HOẠT CHU KỲ NĂNG LƯỢNG QUA LED 2 ----- */
	else if (led_idx == 2) { 
		led->value = set->on_off;

		if (set->on_off == 1) {
			if (!cycle_mode_active) {
				cycle_mode_active = true;
				is_awake = true;
				
				k_work_reschedule(&blink_work, K_NO_WAIT);    
				k_work_reschedule(&cycle_work, K_MSEC(10000));
				
				next_state_on = true;
				k_work_reschedule(&publish_work, K_MSEC(500));

				/* --- TĂNG VÀ HIỂN THỊ SENSOR TẠI LẦN THỨC ĐẦU TIÊN CỦA CHU KỲ --- */
				//simulated_sensor_value += 1;
				LOG_INF("--- [SENSOR]: Bat dau chu ky, gia tri sensor = %d ---", simulated_sensor_value);
				display_sensor_value();
			}
		} else {
			cycle_mode_active = false;
			is_awake = true; 
			
			// Hủy toàn bộ mọi lịch trình đang chờ
			k_work_cancel_delayable(&cycle_work);
			k_work_cancel_delayable(&blink_work);
			k_work_cancel_delayable(&publish_work);
			k_work_cancel_delayable(&suspend_work); // Bổ sung hủy bộ đệm đi ngủ
			
			bt_mesh_resume(); // Ép hệ thống luôn thức
			dk_set_led(2, LED_ON);

			for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
				if (i != 1 && i != 2 && i != 3) {
					dk_set_led(i, led_ctx[i].value ? LED_ON : LED_OFF);
				}
			}
		}
	} 
	else {
		led->value = set->on_off; 
		if (!bt_mesh_model_transition_time(set->transition)) {
			led->remaining = 0;
			if (is_awake && led_idx != 3) {
				dk_set_led(led_idx, set->on_off ? LED_ON : LED_OFF);
			}
		} else {
			led->remaining = set->transition->time;
			if (set->transition->delay) {
				k_work_reschedule(&led->work, K_MSEC(set->transition->delay));
			} else {
				if (is_awake && led_idx != 3) {
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
		if (is_awake && led_idx != 1 && led_idx != 2 && led_idx != 3) {
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
			BT_MESH_MODEL_ONOFF_SRV(&led_ctx[0].srv),
			BT_MESH_MODEL_ONOFF_CLI(&onoff_cli)),
		/* CHÊM VÀO ĐUÔI ELEMENT 1: Khai báo Vendor Client Model để hệ thống biên dịch */
		BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_VND(VND_COMPANY_ID, VND_MODEL_ID_CLI, vnd_cli_ops, &vnd_pub, NULL)
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
		led_ctx[i].value = 0; 
	}

	// Đăng ký toàn bộ hàng đợi
	k_work_init_delayable(&blink_work, blink_handler);
	k_work_init_delayable(&cycle_work, cycle_handler);
	k_work_init_delayable(&publish_work, publish_handler); 
	k_work_init_delayable(&suspend_work, suspend_handler); // BỘ ĐỆM ĐI NGỦ

	/* THÊM: Trỏ con trỏ lưu địa chỉ Vendor Model Client khi khởi động */
	vnd_cli_model = &elements[0].vnd_models[0];

	cycle_mode_active = false;
	is_awake = true;
	
	for (int i = 0; i < ARRAY_SIZE(led_ctx); i++) {
		dk_set_led(i, LED_OFF); 
	}
	dk_set_led(2, LED_ON);

	return &comp;
}