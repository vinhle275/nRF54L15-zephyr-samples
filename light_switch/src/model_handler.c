/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Model handler for the light switch.
 *
 * Instantiates a Generic OnOff Client model for each button on the devkit, as
 * well as the standard Config and Health Server models. Handles all application
 * behavior related to the models.
 */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh/proxy.h>
#include <bluetooth/mesh/models.h>
#include <dk_buttons_and_leds.h>
#include "model_handler.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(model_handler, CONFIG_LOG_DEFAULT_LEVEL);

/* Light switch behavior */

/** Context for a single light switch. */
struct button {
	/** Current light status of the corresponding server. */
	bool status;
	/** Generic OnOff client instance for this switch. */
	struct bt_mesh_onoff_cli client;
};

static void status_handler(struct bt_mesh_onoff_cli *cli,
			   struct bt_mesh_msg_ctx *ctx,
			   const struct bt_mesh_onoff_status *status);

static struct button buttons[] = {
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
	{ .client = BT_MESH_ONOFF_CLI_INIT(&status_handler) },
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw1))
	{ .client = BT_MESH_ONOFF_CLI_INIT(&status_handler) },
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw2))
	{ .client = BT_MESH_ONOFF_CLI_INIT(&status_handler) },
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw3)) && !defined(CONFIG_BT_MESH_LOW_POWER)
	{ .client = BT_MESH_ONOFF_CLI_INIT(&status_handler) },
#endif
};

// static void status_handler(struct bt_mesh_onoff_cli *cli,
// 			   struct bt_mesh_msg_ctx *ctx,
// 			   const struct bt_mesh_onoff_status *status)
// {
// 	struct button *button =
// 		CONTAINER_OF(cli, struct button, client);
// 	int index = button - &buttons[0];

// 	button->status = status->present_on_off;
// 	dk_set_led(index, status->present_on_off);

// 	printk("Button %d: Received response: %s\n", index + 1,
// 	       status->present_on_off ? "on" : "off");
// }

// static void status_handler(struct bt_mesh_onoff_cli *cli,
//                            struct bt_mesh_msg_ctx *ctx,
//                            const struct bt_mesh_onoff_status *status)
// {
//     // Lấy chính xác cấu trúc button từ con trỏ client
//     struct button *btn = CONTAINER_OF(cli, struct button, client);
//     int button_idx = btn - &buttons[0];

//     // Log gốc hiển thị trạng thái của SDK
//     printk("Button %d: Received response: %s\n", button_idx + 1,
//            status->present_on_off ? "on" : "off");

//     /* Nếu Nút 1 (button_idx == 0) nhận được phản hồi từ mạch đèn */
//     if (button_idx == 0) {
        
//         // Lấy trạng thái Tắt/Mở thực tế mà mạch đèn 0x0002 vừa trả về
//         bool is_on = status->present_on_off;

//         LOG_INF("Dong bo LED cua Switch theo mach den: %s", is_on ? "MO" : "TAT");

//         // Gán trạng thái đó cho LED số 1 trên mạch Switch (Mở theo mở, tắt theo tắt)
//         dk_set_led(0, 1); 

//         // Đảm bảo các đèn không liên quan khác đều tắt (Tùy chọn)
//         dk_set_led(1, 1); 
//         dk_set_led(2, 1); 
//         dk_set_led(3, 1); 

//         // Đồng bộ lại trạng thái phần mềm để lần bấm nút tiếp theo mạch hiểu đúng logic
//         buttons[0].status = is_on;
//     }
// }

// --- HỆ THỐNG BIẾN ĐẾM VÀ THEO DÕI LOGIC ---
// --- HỆ THỐNG BIẾN ĐẾM VÀ THEO DÕI LOGIC (ĐÃ FIX LỖI) ---
static uint32_t total_raw_received = 0;  // Tổng số gói tin thực tế đã nhận
static uint32_t duplicate_counter = 0;   // Số gói xác định là trùng lặp dội về
static int current_state = -1;           // Lưu trạng thái hiện tại (-1 là chưa xác định)

static void status_handler(struct bt_mesh_onoff_cli *cli,
                           struct bt_mesh_msg_ctx *ctx,
                           const struct bt_mesh_onoff_status *status)
{
    struct button *btn = CONTAINER_OF(cli, struct button, client);
    int button_idx = btn - &buttons[0];

    /* TRƯỜNG HỢP 1: Xử lý cho riêng mạch đèn 0x0006 */
    if (ctx->addr == 0x0006) {
        bool is_on = status->present_on_off;
        
        // LUÔN LUÔN TĂNG: Cứ nhận được gói từ 0x0006 là đếm
        total_raw_received++;

        // In chi tiết thông số tầng mạng để kiểm tra gói đi qua bao nhiêu bước nhảy (Relay)
        printk("[MESH DEBUG] Nhan packet tu: 0x%04x | TTL con lai: %d | RSSI: %d\n", 
               ctx->addr, ctx->recv_ttl, ctx->recv_rssi);

        if (ctx->recv_ttl < BT_MESH_TTL_DEFAULT) { 
            printk("[RTT COUNTER] ===> GOI TIN NAY DI QUA RELAY!\n");
        } else {
            printk("[RTT COUNTER] ===> GOI TIN GOC (TRUC TIEP)!\n");
        }

        // Kiểm tra trùng lặp dựa trên trạng thái của gói tin liền trước
        if (current_state != -1 && (int)is_on == current_state) {
            duplicate_counter++;
            printk("[RTT COUNTER] ===> GOI TRUNG LAP DO RELAY! | Den: %s | TONG: %u | Trung: %u\n", 
                   is_on ? "MO" : "TAT", total_raw_received, duplicate_counter);
        } else {
            printk("[RTT COUNTER] ===> GOI TIN MOI!            | Den: %s | TONG: %u | Trung: %u\n", 
                   is_on ? "MO" : "TAT", total_raw_received, duplicate_counter);
        }

        // Cập nhật lại mốc trạng thái vừa nhận cho lần sau
        current_state = (int)is_on;

        // --- ĐỒNG BỘ LOGIC LED PHẦN CỨNG ---
        if (is_on) {
           // dk_set_led(0, 0); // Mạch đèn mở -> LED 1 SÁNG (0)
            dk_set_led(1, 1);
            dk_set_led(2, 1);
            dk_set_led(3, 1);
            buttons[0].status = true; 
        } else {
           // dk_set_led(0, 1); // Mạch đèn tắt -> TẮT TOÀN BỘ LED (1)
            dk_set_led(1, 1);
            dk_set_led(2, 1);
            dk_set_led(3, 1);

            for (int i = 0; i < ARRAY_SIZE(buttons); ++i) {
                buttons[i].status = false;
            }
        }
        return; 
    }

    /* TRƯỜNG HỢP 2: Các nút bấm hoặc mạch khác điều khiển (Nếu có) */
    buttons[button_idx].status = status->present_on_off;
    dk_set_led(button_idx, status->present_on_off ? 0 : 1);
}
// static void button_handler_cb(uint32_t pressed, uint32_t changed)
// {
// 	if (!bt_mesh_is_provisioned()) {
// 		return;
// 	}

// 	if (IS_ENABLED(CONFIG_BT_MESH_LOW_POWER) && (pressed & changed & BIT(3))) {
// 		bt_mesh_proxy_identity_enable();
// 		return;
// 	}

// 	for (int i = 0; i < ARRAY_SIZE(buttons); ++i) {
// 		if (!(pressed & changed & BIT(i))) {
// 			continue;
// 		}

// 		struct bt_mesh_onoff_set set = {
// 			.on_off = !buttons[i].status,
// 		};
// 		int err;

// 		/* As we can't know how many nodes are in a group, it doesn't
// 		 * make sense to send acknowledged messages to group addresses -
// 		 * we won't be able to make use of the responses anyway. This also
// 		 * applies in LPN mode, since we can't expect to receive a response
// 		 * in appropriate time.
// 		 */
// 		if (bt_mesh_model_pub_is_unicast(buttons[i].client.model) &&
// 		    !IS_ENABLED(CONFIG_BT_MESH_LOW_POWER)) {
// 			err = bt_mesh_onoff_cli_set(&buttons[i].client, NULL, &set, NULL);
// 		} else {
// 			err = bt_mesh_onoff_cli_set_unack(&buttons[i].client,
// 							  NULL, &set);
// 			if (!err) {
// 				/* There'll be no response status for the
// 				 * unacked message. Set the state immediately.
// 				 */
// 				buttons[i].status = set.on_off;
// 				dk_set_led(i, set.on_off);
// 			}
// 		}

// 		if (err) {
// 			printk("OnOff %d set failed: %d\n", i + 1, err);
// 		}
// 	}
// }



#define TARGET_LIGHT_ADDRESS 0x0006 

static void button_handler_cb(uint32_t pressed, uint32_t changed)
{
    if (!bt_mesh_is_provisioned()) {
        return;
    }

    if (IS_ENABLED(CONFIG_BT_MESH_LOW_POWER) && (pressed & changed & BIT(3))) {
        bt_mesh_proxy_identity_enable();
        return;
    }

    for (int i = 0; i < ARRAY_SIZE(buttons); ++i) {
        if (!(pressed & changed & BIT(i))) {
            continue;
        }

        struct bt_mesh_onoff_set set = {
            .on_off = !buttons[i].status,
        };
        int err;

        if (i == 0) {
            LOG_INF("Nut 1 duoc nhan! Dang dong goi gui toi dia chi 0x0006...");
            
            struct bt_mesh_msg_ctx ctx = {
                .addr = TARGET_LIGHT_ADDRESS, 
                .app_idx = buttons[i].client.model->keys[0], 
                .send_ttl = BT_MESH_TTL_DEFAULT,
            };

            // Gửi bản tin Set (Có yêu cầu ACK trả về)
            err = bt_mesh_onoff_cli_set(&buttons[i].client, &ctx, &set, NULL);
            
            if (err) {
                LOG_ERR("Gui tin that bai! Ma loi: %d", err);
            } else {
                LOG_INF("Da phat tin thanh cong. Dang cho phan hoi...");
                buttons[i].status = set.on_off;
                dk_set_led(i, set.on_off);
            }
        } else {
            /* Các nút 2, 3, 4 giữ nguyên */
            if (bt_mesh_model_pub_is_unicast(buttons[i].client.model) &&
                !IS_ENABLED(CONFIG_BT_MESH_LOW_POWER)) {
                err = bt_mesh_onoff_cli_set(&buttons[i].client, NULL, &set, NULL);
            } else {
                err = bt_mesh_onoff_cli_set_unack(&buttons[i].client, NULL, &set);
                if (!err) {
                    buttons[i].status = set.on_off;
                    dk_set_led(i, set.on_off);
                }
            }
        }
    }
}

static void onoff_cli_status(struct bt_mesh_onoff_cli *cli,
                             struct bt_mesh_msg_ctx *ctx,
                             const struct bt_mesh_onoff_status *status)
{
    // Bất kì mạch nào gửi gói STATUS về cho Switch đều sẽ kích hoạt dòng này
    LOG_INF("Nhan duoc goi tin phan hoi tu dia chi: 0x%04x", ctx->addr);

    /* KIỂM TRA: Nếu đúng là mạch đèn 0x0002 phản hồi */
    if (ctx->addr == 0x0006) {
        
        LOG_INF("Xac nhan tu mach 0x0006! Tien hanh tat toan bo LED tren mach Switch...");

        // Gọi lệnh tắt tường minh cho từng đèn LED phần cứng
        //dk_set_led(0, 0); // Tắt LED 1
        dk_set_led(1, 0); // Tắt LED 2
        dk_set_led(2, 0); // Tắt LED 3
        dk_set_led(3, 0); // Tắt LED 4

        // Reset lại trạng thái phần mềm của tất cả các nút về 0 (Tắt)
        for (int i = 0; i < ARRAY_SIZE(buttons); ++i) {
            buttons[i].status = 0;
        }
    }
}
/* Set up a repeating delayed work to blink the DK's LEDs when attention is
 * requested.
 */
static struct k_work_delayable attention_blink_work;
static bool attention;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
		BIT(0),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw1))
		BIT(1),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw2))
		BIT(2),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw3))
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
	/* Will stop rescheduling blink timer */
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
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
	BT_MESH_ELEM(1,
		     BT_MESH_MODEL_LIST(
			     BT_MESH_MODEL_CFG_SRV,
			     BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
			     BT_MESH_MODEL_ONOFF_CLI(&buttons[0].client)),
		     BT_MESH_MODEL_NONE),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw1))
	BT_MESH_ELEM(2,
		     BT_MESH_MODEL_LIST(
			     BT_MESH_MODEL_ONOFF_CLI(&buttons[1].client)),
		     BT_MESH_MODEL_NONE),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw2))
	BT_MESH_ELEM(3,
		     BT_MESH_MODEL_LIST(
			     BT_MESH_MODEL_ONOFF_CLI(&buttons[2].client)),
		     BT_MESH_MODEL_NONE),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw3)) && !defined(CONFIG_BT_MESH_LOW_POWER)
	BT_MESH_ELEM(4,
		     BT_MESH_MODEL_LIST(
			     BT_MESH_MODEL_ONOFF_CLI(&buttons[3].client)),
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
	static struct button_handler button_handler = {
		.cb = button_handler_cb,
	};

	dk_button_handler_add(&button_handler);
	k_work_init_delayable(&attention_blink_work, attention_blink);

	return &comp;
}
