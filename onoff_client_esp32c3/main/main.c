/* main.c - Application main entry point */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"

#include "board.h"
#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"

#define TAG "EXAMPLE"

#define CID_ESP 0x02E5

#define GROUP_ADDR 0xC000 ///
#define BUTTON_GPIO GPIO_NUM_20 ///
#define RESET_GPIO GPIO_NUM_21 ///
static bool send_fire_alarm_flag = false; ///
static bool send_reset_alarm_flag = false; ///
static uint8_t dev_uuid[16] = { 0x32, 0x10 };

static uint8_t restore_state =0;
static void led_apply_state(uint8_t onoff); ///

static struct example_info_store {
    uint16_t net_idx;   /* NetKey Index */
    uint16_t app_idx;   /* AppKey Index */
    uint8_t  onoff;     /* Remote OnOff */
    uint8_t  tid;       /* Message TID */
} __attribute__((packed)) store = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .onoff = LED_OFF,
    .tid = 0x0,
};

static nvs_handle_t NVS_HANDLE;
static const char * NVS_KEY = "onoff_client";

static esp_ble_mesh_client_t onoff_client;

static void send_fire_alarm(void); /// Prototype cho func
static void send_reset_alarm(void); /// Prototype cho func

static esp_ble_mesh_cfg_srv_t config_server = {
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
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

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 1, ROLE_NODE);

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

/* Disable OOB security for SILabs Android app */
static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
#if 0
    .output_size = 4,
    .output_actions = ESP_BLE_MESH_DISPLAY_NUMBER,
    .input_size = 4,
    .input_actions = ESP_BLE_MESH_PUSH,
#else
    .output_size = 0,
    .output_actions = 0,
#endif
};

static void mesh_example_info_store(void)
{
    ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &store, sizeof(store));
}

static void mesh_example_info_restore(void)
{
    esp_err_t err = ESP_OK;
    bool exist = false;

    err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &store, sizeof(store), &exist);
    if (err != ESP_OK) {
        return;
    }

    if (exist) {
        ESP_LOGI(TAG, "Restore, net_idx 0x%04x, app_idx 0x%04x, onoff %u, tid 0x%02x",
            store.net_idx, store.app_idx, store.onoff, store.tid);
    }
}

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG, "net_idx: 0x%04x, addr: 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08" PRIx32, flags, iv_index);
    board_led_operation(LED_G, LED_OFF);
    store.net_idx = net_idx;
    /* mesh_example_info_store() shall not be invoked here, because if the device
     * is restarted and goes into a provisioned state, then the following events
     * will come:
     * 1st: ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT
     * 2nd: ESP_BLE_MESH_PROV_REGISTER_COMP_EVT
     * So the store.net_idx will be updated here, and if we store the mesh example
     * info here, the wrong app_idx (initialized with 0xFFFF) will be stored in nvs
     * just before restoring it.
     */
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        mesh_example_info_restore(); /* Restore proper mesh example info */
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
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    default:
        break;
    }
}


static void example_ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                               esp_ble_mesh_generic_client_cb_param_t *param)
{
    ESP_LOGI(TAG, "Generic client, event %u, error code %d, opcode is 0x%04" PRIx32,
        event, param->error_code, param->params->opcode);

    switch (event) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT");
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET, onoff %d", param->status_cb.onoff_status.present_onoff);
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT");
    ///    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
    ///        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET, onoff %d", param->status_cb.onoff_status.present_onoff);
    ///    }
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
        param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {

        uint8_t new_state = param->status_cb.onoff_status.present_onoff;
        ESP_LOGI(TAG, "Update onoff = %d", new_state);
        store.onoff = new_state;
        mesh_example_info_store();   // SAVE STATE
        led_apply_state(new_state);
    }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT");
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT");
        /*if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {/// SET -> SET_UNACK
           ///example_ble_mesh_send_gen_onoff_set();
             send_fire_alarm(); /// khúc này ngầm hiểu là non-static vì khai báo sau
        }
        break;*/ /// ignore it 
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
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);
            if (param->value.state_change.mod_app_bind.company_id == 0xFFFF &&
                param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
                store.app_idx = param->value.state_change.mod_app_bind.app_idx;
                mesh_example_info_store(); /* Store proper mesh example info */
            }
            break;
        default:
            break;
        }
    }
}

static esp_err_t ble_mesh_init(void) ///
{
    esp_err_t err = ESP_OK;

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_generic_client_callback(example_ble_mesh_generic_client_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
        return err;
    }

    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh Node initialized");

    board_led_operation(LED_G, LED_ON);

    return err;
}

static void send_fire_alarm(void) ///
{   
    /*if (send_fire_alarm_flag) {
        ESP_LOGI(TAG, "FIRE ALARM already sent");
        return;
    }*/

    esp_ble_mesh_generic_client_set_state_t set = {0};
    esp_ble_mesh_client_common_param_t common = {0};

    if (store.net_idx == ESP_BLE_MESH_KEY_UNUSED ||
        store.app_idx == ESP_BLE_MESH_KEY_UNUSED) {
        ESP_LOGW(TAG, "Mesh not ready");
        return;
    }

    common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;
    common.model = onoff_client.model;

    common.ctx.net_idx = store.net_idx;
    common.ctx.app_idx = store.app_idx;
    common.ctx.addr = GROUP_ADDR;
    common.ctx.send_ttl = 5;
    common.msg_timeout = 0;

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
    common.msg_role = ROLE_NODE;
#endif

    set.onoff_set.op_en = false;
    set.onoff_set.onoff = 1;   // FIRE ALERT
    set.onoff_set.tid = store.tid++;
   
    esp_err_t err; 
    err = esp_ble_mesh_generic_client_set_state(&common, &set);

    if (err) {
        ESP_LOGE(TAG, "Send FIRE ALARM failed");
        return;
    }
    else 
    {
        send_fire_alarm_flag = true;
        send_reset_alarm_flag = false;
        ESP_LOGI(TAG, "FIRE ALARM SENT WITH NET_IDX: 0x%04x, APP_IDX: 0x%04x", store.net_idx, store.app_idx);
    }
    
}
static void send_reset_alarm(void) ///
{   
    /*if (send_reset_alarm_flag) {
        ESP_LOGI(TAG, "RESET ALARM already sent");
        return;
    }*/

    esp_ble_mesh_generic_client_set_state_t set = {0};
    esp_ble_mesh_client_common_param_t common = {0};

    if (store.net_idx == ESP_BLE_MESH_KEY_UNUSED ||
        store.app_idx == ESP_BLE_MESH_KEY_UNUSED) {
        ESP_LOGW(TAG, "Mesh not ready");
        return;
    }

    common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;
    common.model = onoff_client.model;

    common.ctx.net_idx = store.net_idx;
    common.ctx.app_idx = store.app_idx;
    common.ctx.addr = GROUP_ADDR;
    common.ctx.send_ttl = 5;
    common.msg_timeout = 0;

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
    common.msg_role = ROLE_NODE;
#endif

    set.onoff_set.op_en = false;
    set.onoff_set.onoff = 0;   // RESET ALERT
    set.onoff_set.tid = store.tid++;
   
    esp_err_t err; 
    err = esp_ble_mesh_generic_client_set_state(&common, &set);

    if (err) {
        ESP_LOGE(TAG, "Send RESET ALARM failed");
        return;
    }
    else 
    {
        send_reset_alarm_flag = true;
        send_fire_alarm_flag = false; 
        ESP_LOGI(TAG, "RESET ALARM SENT");
    }
    
}
static void button_init(void) ///
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO) |
                        (1ULL << RESET_GPIO), 
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,// avoid floating
        .intr_type = GPIO_INTR_DISABLE, // no interrupt, we'll poll the button state in a task
    };

    gpio_config(&io_conf);
}
static void taskBtn(void *arg)
{
    //led built in has set to light down-edge trigger
    uint8_t last_fire = 1;
    uint8_t last_reset = 1;

    while (1) {

        /* ===== FIRE BUTTON ===== */
        uint8_t now_fire = gpio_get_level(BUTTON_GPIO);
        if (last_fire == 1 && now_fire == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (gpio_get_level(BUTTON_GPIO) == 0) {
                ESP_LOGI(TAG, "FIRE BUTTON");
                send_fire_alarm();

                gpio_set_level(GPIO_NUM_8, 1);
                while (gpio_get_level(BUTTON_GPIO) == 0)
                    vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(GPIO_NUM_8, 0);
            }
        }
        last_fire = now_fire;

        /* ===== RESET BUTTON ===== */
        uint8_t now_reset = gpio_get_level(RESET_GPIO);
        if (last_reset == 1 && now_reset == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (gpio_get_level(RESET_GPIO) == 0) {
                ESP_LOGI(TAG, "RESET BUTTON");
                send_reset_alarm();

                gpio_set_level(GPIO_NUM_8, 1); 
                while (gpio_get_level(RESET_GPIO) == 0)
                    vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(GPIO_NUM_8, 0);
            }
        }
        last_reset = now_reset;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
static void led_apply_state(uint8_t onoff)
{
    ESP_LOGI(TAG, "Applying LED state: %s", onoff ? "ON" : "OFF");
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_8, onoff ? 1 : 0);
}
void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing...");

    board_init();

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    /* Open nvs namespace for storing/restoring mesh example info */
    err = ble_mesh_nvs_open(&NVS_HANDLE);
    if (err) {
        return;
    }
    ble_mesh_get_dev_uuid(dev_uuid);
    // reset state led built in to check if the state is restored properly
    gpio_reset_pin(GPIO_NUM_8);
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_8, 0);

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }

    store.tid = 0; // Initialize TID to 0 if settings RPL is on

    button_init(); ///
    xTaskCreate(taskBtn, "taskBtn", 4096, NULL, 5, NULL); /// 

}
