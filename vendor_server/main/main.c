/*VENDOR_SERVER/ main.c - Application main entry point */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include  "esp_timer.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include "board.h"
#include "ble_mesh_example_init.h"

#include "sensor.h"
#include "alarm.h"

#define TAG "EXAMPLE"

#define CID_ESP     0x02E5

#define ESP_BLE_MESH_VND_MODEL_ID_CLIENT    0x0002
#define ESP_BLE_MESH_VND_MODEL_ID_SERVER    0x0001

#define ESP_BLE_MESH_VND_MODEL_OP_SEND      ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

#define GROUP_ADDR_VENDOR 0xC000

static bool appkey_bound = false;
static bool model_bound = false;
static bool sub_done = false;
bool mesh_ready = false;
bool sensor_task_started = false;

static uint16_t prov_net_idx = ESP_BLE_MESH_KEY_UNUSED;
static uint16_t prov_app_idx = ESP_BLE_MESH_KEY_UNUSED;//đang sửa 

typedef struct __attribute__((packed)) {

    uint8_t  cmd;
    uint16_t mq2_adc;
    int16_t  temp_x10;
    int16_t  humi_x10;
    uint8_t  fire_level;

} fire_msg_t;

typedef enum {
    NODE_NORMAL = 0,
    NODE_WARNING,
    NODE_FIRE,
    NODE_OFFLINE
} node_state_t;

#define MQ2_FIRE_ON     2800
#define MQ2_FIRE_OFF    2200

#define FIRE_LEVEL_ON   2
#define FIRE_LEVEL_OFF  1

#define FIRE_CONFIRM_CNT 3

typedef struct {
    uint16_t mq2;
    int16_t temp;
    int16_t humi;
    uint8_t fire_level;

    bool local_fire;
    uint8_t fire_cnt;
    uint32_t last_update_ms;
} sensor_state_t;

static sensor_state_t self_state;

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x32, 0x10 };

static esp_ble_mesh_cfg_srv_t config_server = {
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_DISABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(vnd_pub, 32, ROLE_NODE);

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SEND, 3),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, ESP_BLE_MESH_VND_MODEL_ID_SERVER,
    vnd_op, &vnd_pub, NULL),
};

/*static esp_ble_mesh_model_t vnd_models[] = {
    {
        .vnd.company_id = CID_ESP,
        .vnd.model_id = ESP_BLE_MESH_VND_MODEL_ID_SERVER,

        .pub = &vnd_pub,
        .op = vnd_op,
        .cb = NULL,
        .user_data = NULL,

        .keys = {
            ESP_BLE_MESH_KEY_UNUSED
        },

        .groups = {
            ESP_BLE_MESH_ADDR_UNASSIGNED
        },
    },
};
*/
static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

static void send_fire_alert(void)
{
    fire_msg_t msg;

    msg.cmd = 0x01;

    msg.mq2_adc = self_state.mq2;
    msg.temp_x10 = self_state.temp;
    msg.humi_x10 = self_state.humi;
    msg.fire_level = self_state.fire_level;

    if (!mesh_ready) {
        ESP_LOGW(TAG, "Mesh not ready");
        return;
    }

    esp_ble_mesh_msg_ctx_t ctx = {0};

    ctx.net_idx  = prov_net_idx;
    ctx.app_idx  = prov_app_idx;

    ctx.addr     = GROUP_ADDR_VENDOR;

    ctx.send_ttl = 3;

    esp_err_t err =
        esp_ble_mesh_server_model_send_msg(
            &vnd_models[0],
            &ctx,
            ESP_BLE_MESH_VND_MODEL_OP_STATUS,
            sizeof(msg),
            (uint8_t *)&msg);

    if (err != ESP_OK) {
        ESP_LOGE(TAG,
            "Failed to send FIRE ALERT");
        return;
    }

    ESP_LOGW(TAG,
        "FIRE ALERT PUSHED | MQ2=%d TEMP=%.1f FIRE=%d",
        self_state.mq2,
        self_state.temp / 10.0,
        self_state.fire_level);
}

static void send_vendor_sensor_data(sensor_data_t *data)
{
    fire_msg_t msg;

    msg.cmd = 0x01;
    msg.mq2_adc = data->mq2_adc;
    msg.temp_x10 = data->temp_x10 * 10;
    msg.humi_x10 = data->humi_x10 * 10;
    msg.fire_level = self_state.fire_level;

    if (!mesh_ready || prov_app_idx == ESP_BLE_MESH_KEY_UNUSED ||
    prov_net_idx == ESP_BLE_MESH_KEY_UNUSED) {
    ESP_LOGW(TAG, "DROP SEND - MESH NOT READY");
    ESP_LOGW(TAG,"prov_app_idx = %d, mesh_ready = %d", prov_app_idx, mesh_ready);
    return;
    }
    esp_ble_mesh_msg_ctx_t ctx = {0};

    ctx.net_idx  = prov_net_idx;
    ctx.app_idx  = prov_app_idx;
    ctx.addr     = GROUP_ADDR_VENDOR;
    ctx.send_ttl = 3;
    //ctx.recv_ttl = 3;
    
    ESP_LOGD(TAG, "Sending to NetIdx: 0x%04x, AppIdx: 0x%04x, Addr: 0x%04x", 
             ctx.net_idx, ctx.app_idx, ctx.addr);

    esp_err_t err = esp_ble_mesh_server_model_send_msg(
        &vnd_models[0],
        &ctx,
        ESP_BLE_MESH_VND_MODEL_OP_STATUS,
        sizeof(msg),
        (uint8_t *)&msg);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send vendor data");
        return;
    }

    ESP_LOGI(TAG,
        "Vendor data sent | MQ2=%d Temp=%.1f Humi=%.1f Fire=%d",
        data->mq2_adc,
        data->temp_x10 / 10.0,
        data->humi_x10 / 10.0,
        data->fire_level);
}

static void sensor_task(void *arg)
{
    sensor_data_t data;

    while (1) {

    if (sensor_read(&data)) {

            self_state.mq2 = data.mq2_adc;
            self_state.temp = data.temp_x10;
            self_state.humi = data.humi_x10;
            self_state.fire_level = data.fire_level;

                bool fire_detect_raw =
                    (data.fire_level >= FIRE_LEVEL_ON) ||
                    (data.mq2_adc >= MQ2_FIRE_ON);

                bool fire_clear_raw =
                    (data.fire_level <= FIRE_LEVEL_OFF) &&
                    (data.mq2_adc <= MQ2_FIRE_OFF);

                if (fire_detect_raw) {
                    if (self_state.fire_cnt < FIRE_CONFIRM_CNT) {
                        self_state.fire_cnt++;
                    }
                } else if (fire_clear_raw) {
                    if (self_state.fire_cnt > 0) {
                        self_state.fire_cnt--;
                    }
                }

        self_state.local_fire = (self_state.fire_cnt >= FIRE_CONFIRM_CNT);

        self_state.last_update_ms = esp_timer_get_time() / 1000;
        
    ESP_LOGI(TAG,
        "[SENSOR] MQ2=%d TEMP=%.1f HUMI=%.1f FIRE=%d",
        self_state.mq2,
        self_state.temp / 10.0,
        self_state.humi / 10.0,
        self_state.fire_level);

        // Đã tắt việc chủ động gửi dữ liệu định kỳ. 
        // Server sẽ chỉ gửi dữ liệu khi nhận được gói tin yêu cầu (cmd 0x10) từ Client.
        // send_vendor_sensor_data(&data);
                }
        else {
    ESP_LOGE(TAG, "SENSOR READ FAILED");
    }
        if (self_state.local_fire) {
        //send_fire_alert();
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
}
}
static void start_sensor_task()
{
    if( sensor_task_started ) {
        return;
    }
    if (!sensor_task_started) {
        xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5, NULL);
        sensor_task_started = true;
    }
}

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    prov_net_idx = net_idx;
    ESP_LOGI(TAG, "net_idx 0x%03x, addr 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags 0x%02x, iv_index 0x%08" PRIx32, flags, iv_index);
    board_led_operation(LED_G, LED_OFF);
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        /* Di chuyển kiểm tra provision vào đây để đảm bảo stack đã sẵn sàng trả lời */
        if (esp_ble_mesh_node_is_provisioned()) {
            mesh_ready = true;
            prov_net_idx = 0x0000; 
            prov_app_idx = 0x0000; 
        }
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
            param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
            param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
        prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
            param->node_prov_complete.flags, param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_RESET_EVT");
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    default:
        break;
    }
}

static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        appkey_bound = true;
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);

        prov_app_idx = param->value.state_change.appkey_add.app_idx;
        prov_net_idx = param->value.state_change.appkey_add.net_idx;

            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        if (param->value.state_change.mod_app_bind.company_id == CID_ESP &&
    param->value.state_change.mod_app_bind.model_id ==
        ESP_BLE_MESH_VND_MODEL_ID_SERVER) {

    model_bound = true;

    ESP_LOGI(TAG, "VENDOR MODEL BOUND");
}
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);

            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:

            if (param->value.state_change.mod_sub_add.model_id ==
                ESP_BLE_MESH_VND_MODEL_ID_SERVER &&
                param->value.state_change.mod_sub_add.company_id ==
                CID_ESP) {

                sub_done = true;

                ESP_LOGI(TAG, "VENDOR MODEL SUB DONE");
            }
            break;
        case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_STATUS:
            ESP_LOGI(TAG, "COMP DONE");
            break;
        default:
            break;
        }
        if (appkey_bound && model_bound && sub_done) {
        if (!mesh_ready) {

                ESP_LOGI(TAG, "CONFIG FLOW DONE");

                mesh_ready = true;
                start_sensor_task();
            }
        }
    }
}
static void example_ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                             esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
    /*    if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_SEND) {
            uint16_t tid = *(uint16_t *)param->model_operation.msg;
            ESP_LOGI(TAG, "Recv 0x%06" PRIx32 ", tid 0x%04x", param->model_operation.opcode, tid);
            esp_err_t err = esp_ble_mesh_server_model_send_msg(&vnd_models[0],
                    param->model_operation.ctx, ESP_BLE_MESH_VND_MODEL_OP_STATUS,
                    sizeof(tid), (uint8_t *)&tid);
            if (err) {
                ESP_LOGE(TAG, "Failed to send message 0x%06x", ESP_BLE_MESH_VND_MODEL_OP_STATUS);
            }
        }
    */

        if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_SEND) {

           /*fire_msg_t *msg = (fire_msg_t *)param->model_operation.msg;
            uint16_t src_addr = param->model_operation.ctx->addr;
    
            ESP_LOGI(TAG, "Vendor msg received");
            ESP_LOGI(TAG, "CMD         : %d", msg->cmd);
            ESP_LOGI(TAG, "MQ2 ADC     : %d", msg->mq2_adc);
            ESP_LOGI(TAG, "Temperature : %.1f C", msg->temp_x10 / 10.0);
            ESP_LOGI(TAG, "Humidity    : %.1f %%", msg->humi_x10 / 10.0);
            ESP_LOGI(TAG, "Fire Level  : %d", msg->fire_level);*/
            uint8_t *msg = (uint8_t *)param->model_operation.msg;
            uint8_t cmd = msg[0];
            uint16_t tid = ((uint16_t)msg[1] << 8) | msg[2];
            ESP_LOGI(TAG,"GET received | CMD=0x%02X TID=0x%04X", cmd, tid);

    switch (cmd) {

    case 0x01:
        ESP_LOGW(TAG, "FIRE ALERT RECEIVED!");
        break;

    case 0x02:
        ESP_LOGI(TAG, "TEMP UPDATE");
        break;
    case 0x10: 
        {
        fire_msg_t resp;
            resp.cmd = 0x01;
            resp.mq2_adc = self_state.mq2;
            resp.temp_x10 = self_state.temp;
            resp.humi_x10 = self_state.humi;
            resp.fire_level = self_state.fire_level;    


        esp_err_t err =   esp_ble_mesh_server_model_send_msg(
                &vnd_models[0],
                param->model_operation.ctx,
                ESP_BLE_MESH_VND_MODEL_OP_STATUS,
                sizeof(resp),
                (uint8_t *)&resp);

        if (err != ESP_OK) {ESP_LOGE(TAG, "Failed to send STATUS");}
        else {

            ESP_LOGI(TAG,
                "STATUS sent");

            ESP_LOGI(TAG,
                "LOCAL_FIRE=%d FIRE=%d MQ2=%d TEMP=%.1f",
                self_state.local_fire,
                self_state.fire_level,
                self_state.mq2,
                self_state.temp / 10.0);
            }
        
        }
        break;
    default:
        ESP_LOGW(TAG, "Unknown vendor cmd: 0x%02X", cmd);
        break;
    }

/*    uint8_t status[2];

    status[0] = msg->cmd;
    status[1] = 0xAA;

    esp_err_t err = esp_ble_mesh_server_model_send_msg(
            &vnd_models[0],
            param->model_operation.ctx,
            ESP_BLE_MESH_VND_MODEL_OP_STATUS,
            sizeof(status),
            status);

    if (err) {
        ESP_LOGE(TAG, "Failed to send STATUS");
    }*/
}
        break;
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG, "Failed to send message 0x%06" PRIx32, param->model_send_comp.opcode);
            break;
        }
        ESP_LOGI(TAG, "Send 0x%06" PRIx32, param->model_send_comp.opcode);
        break;
    default:
        break;
    }
}


static esp_err_t ble_mesh_init(void)
{
    esp_err_t err;

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);
    esp_ble_mesh_register_custom_model_callback(example_ble_mesh_custom_model_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return err;
    }

    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node");
        return err;
    }

    board_led_operation(LED_G, LED_ON);

    ESP_LOGI(TAG, "BLE Mesh Node initialized");
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing...");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    board_init();

    err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    ble_mesh_get_dev_uuid(dev_uuid);

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }
    sensor_init();
    alarm_init();
    start_sensor_task();
}
