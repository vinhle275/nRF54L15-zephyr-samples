/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/dk_prov.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/drivers/gpio.h>
#include "model_handler.h"
#include "smp_bt.h"

/* Khai báo cấu trúc driver GPIO cấu hình trực tiếp chân để ép ngắt dòng */
#include <zephyr/device.h>

static void bt_ready(int err)
{
	if (err) {
		return;
	}

	/* Khởi tạo LED và Button tạm thời phục vụ cho lúc khởi tạo / Provisioning */
	err = dk_leds_init();
	if (err) {
		return;
	}

	err = dk_buttons_init(NULL);
	if (err) {
		return;
	}

	err = bt_mesh_init(bt_mesh_dk_prov_init(), model_handler_init());
	if (err) {
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

	/* * [CỰC KỲ QUAN TRỌNG] ĐỂ GIẢM DÒNG XUỐNG THẤP NHẤT TRÊN BO MẠCH THỰC TẾ:
	 * Sau khi thiết bị đã nạp cấu hình Mesh (hoặc chạy ổn định), bạn cần tắt hẳn LED trạng thái.
	 * Ở đây chúng ta ép tắt tất cả các đèn LED trên kit nhằm triệt tiêu dòng tiêu thụ qua trở treo.
	 */
	dk_set_leds(0x00); 

	/* * Mẹo nâng cao: Nếu thiết bị của bạn là cảm biến pin ko cần nhấn nút liên tục, 
	 * sau khi chạy xong bạn có thể dùng lệnh giải phóng chân GPIO của nút nhấn 
	 * để tránh dòng rò chạy qua điện trở kéo Pull-up nội bộ (Internal Pull-ups ngốn ~10-40uA/chân).
	 * Ví dụ: nrf_gpio_cfg_default(PIN_NUMBER);
	 */
}

int main(void)
{
	int err;

	err = bt_enable(bt_ready);
	if (err) {
		// Xử lý lỗi ngầm không printk
	}

	return 0;
}

