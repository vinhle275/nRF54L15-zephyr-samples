// /*
//  * Copyright (c) 2019 Nordic Semiconductor ASA
//  *
//  * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
//  */

// #include <zephyr/bluetooth/bluetooth.h>
// #include <bluetooth/mesh/models.h>
// #include <bluetooth/mesh/dk_prov.h>
// #include <dk_buttons_and_leds.h>
// #include <zephyr/drivers/gpio.h>
// #include "model_handler.h"
// #include "smp_bt.h"

// /* Khai báo cấu trúc driver GPIO cấu hình trực tiếp chân để ép ngắt dòng */
// #include <zephyr/device.h>

// static void bt_ready(int err)
// {
// 	if (err) {
// 		return;
// 	}

// 	/* Khởi tạo LED và Button tạm thời phục vụ cho lúc khởi tạo / Provisioning */
// 	err = dk_leds_init();
// 	if (err) {
// 		return;
// 	}

// 	err = dk_buttons_init(NULL);
// 	if (err) {
// 		return;
// 	}

// 	err = bt_mesh_init(bt_mesh_dk_prov_init(), model_handler_init());
// 	if (err) {
// 		return;
// 	}

// 	if (IS_ENABLED(CONFIG_SETTINGS)) {
// 		settings_load();
// 	}

// 	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

// 	/* * [CỰC KỲ QUAN TRỌNG] ĐỂ GIẢM DÒNG XUỐNG THẤP NHẤT TRÊN BO MẠCH THỰC TẾ:
// 	 * Sau khi thiết bị đã nạp cấu hình Mesh (hoặc chạy ổn định), bạn cần tắt hẳn LED trạng thái.
// 	 * Ở đây chúng ta ép tắt tất cả các đèn LED trên kit nhằm triệt tiêu dòng tiêu thụ qua trở treo.
// 	 */
// 	dk_set_leds(0x00); 

// 	/* * Mẹo nâng cao: Nếu thiết bị của bạn là cảm biến pin ko cần nhấn nút liên tục, 
// 	 * sau khi chạy xong bạn có thể dùng lệnh giải phóng chân GPIO của nút nhấn 
// 	 * để tránh dòng rò chạy qua điện trở kéo Pull-up nội bộ (Internal Pull-ups ngốn ~10-40uA/chân).
// 	 * Ví dụ: nrf_gpio_cfg_default(PIN_NUMBER);
// 	 */
// }

// int main(void)
// {
// 	int err;

// 	err = bt_enable(bt_ready);
// 	if (err) {
// 		// Xử lý lỗi ngầm không printk
// 	}

// 	return 0;
// }



#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>

/* Lấy thông tin LED từ Device Tree (alias led0) */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

void main(void)
{
    /* Kiểm tra xem thiết bị GPIO đã sẵn sàng chưa */
    if (!gpio_is_ready_dt(&led)) {
        return;
    }

    /* * Cấu hình chân LED là đầu ra.
     * Vì trong file overlay đã định nghĩa GPIO_ACTIVE_LOW cho led_0,
     * mức logic 0 của Zephyr sẽ tương đương mức vật lý 1 (HIGH) -> Đèn tắt.
     * mức logic 1 của Zephyr sẽ tương đương mức vật lý 0 (LOW) -> Đèn sáng.
     */
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    while (1) {
        /* ====================================================
         * CHẾ ĐỘ 1: 10 GIÂY THỨC - Đèn nhấp nháy nhanh
         * ==================================================== */
        /* Lặp 50 lần, mỗi lần mất 200ms (100ms sáng + 100ms tắt) => Tổng 10 giây */
        for (int i = 0; i < 50; i++) {
            /* Kéo chân xuống mức 0 (Vật lý) -> Đèn Sáng */
            gpio_pin_set_dt(&led, 1); 
            k_msleep(100);
            
            /* Kéo chân lên mức 1 (Vật lý) -> Đèn Tắt */
            gpio_pin_set_dt(&led, 0); 
            k_msleep(100);
        }

        /* ====================================================
         * CHẾ ĐỘ 2: 10 GIÂY NGỦ - Đèn tắt, dòng 2-4uA
         * ==================================================== */
        /* Đảm bảo trạng thái đèn ở mức vật lý 1 (Đèn Tắt) theo đúng yêu cầu */
        gpio_pin_set_dt(&led, 1);

        /* * Hàm k_msleep sẽ nhường quyền điều khiển (yield) cho Idle thread.
         * Vì bạn đã bật CONFIG_PM=y, chip nRF54L15 sẽ tự động đi vào
         * chế độ System ON (Deep Sleep) trong 10 giây này.
         */
        k_msleep(10000); 
    }
}