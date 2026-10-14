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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#include "dht.h"
#include "esp_adc/adc_oneshot.h"
#define TAG "EXAMPLE"

#define CID_ESP 0x02E5

#define GROUP_ADDR 0xC000 ///

#define MQ2_GPIO ADC_CHANNEL_6 /// GPIO34 on esp32 
#define DHT_GPIO GPIO_NUM_25 ///

static adc_oneshot_unit_handle_t adc1_handle; ///
static float current_temp = 0;
static float last_temp = 0;
static int mq2_value = 0;
static bool fire_active = false; 
static bool mesh_ready = false;
static bool sensor_task_started = false;
static bool send_fire_alarm_flag = false; ///
static bool send_reset_alarm_flag = false; ///
static uint8_t dev_uuid[16] = { 0x32, 0x10 };

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
static void sensor_task(void *arg);

static esp_ble_mesh_cfg_srv_t config_server = {
    /* 2 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(1, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(1, 20),
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
        if (store.app_idx != ESP_BLE_MESH_KEY_UNUSED) {
        ESP_LOGI(TAG, "Mesh persistence restored");
        mesh_ready = true;
        xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
        sensor_task_started = true;
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
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET, onoff %d", param->status_cb.onoff_status.present_onoff);
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
                mesh_ready = true; /* Mesh is ready to send messages */
                ESP_LOGI(TAG, "Mesh is ready to send messages");
                if (!sensor_task_started) {
                    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL); 
                    sensor_task_started = true;
                }/// Start sensor task when mesh is ready
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

static void mq2_init(void) ///
{
    adc_oneshot_unit_init_cfg_t adc1_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_new_unit(&adc1_config, &adc1_handle);

    adc_oneshot_chan_cfg_t mq2_channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(adc1_handle, MQ2_GPIO, &mq2_channel_config);
}

static int read_mq2_value(void) ///
{
    int adc_raw = 0;

    adc_oneshot_read(adc1_handle, MQ2_GPIO, &adc_raw);
    return adc_raw;
}

static float read_dht11_temperature(void)
{
    float temperature = 0;
    float humidity = 0;

    esp_err_t err = dht_read_float_data(
        DHT_TYPE_AM2301, //or DHT_TYPE_AM2301, DHT_TYPE_SI7021
        DHT_GPIO,
        &humidity,
        &temperature);

    if (err == ESP_OK) {

        ESP_LOGI(TAG,
            "DHT OK temp=%.1f hum=%.1f",
            temperature,
            humidity);

        return temperature;
    }

    ESP_LOGE(TAG, "DHT ERROR: %s", esp_err_to_name(err));

    return -100;
}

static bool detect_fire(void)
{   
    ESP_LOGI(TAG, "Reading sensors...");
    mq2_value = read_mq2_value();
    ESP_LOGI(TAG, "Done reading MQ2 value: %d", mq2_value);
    current_temp = read_dht11_temperature();
    ESP_LOGI(TAG, "Done reading DHT11 temperature: %.1f", current_temp);

    float delta_temp = current_temp - last_temp;

    ESP_LOGI(TAG,
        "MQ2=%d Temp=%.1f Delta=%.1f",
        mq2_value,
        current_temp,
        delta_temp);

    bool fire = false;

    if (mq2_value > 2500 &&
        (current_temp > 38 || delta_temp > 1))
    {
        fire = true;
    }

    last_temp = current_temp;

    return fire;
}
static void sensor_task(void *arg)
{
    while (1) {
        bool fire = detect_fire();

        if (fire && !fire_active) {

            ESP_LOGW(TAG, "FIRE DETECTED");

            send_fire_alarm();

            fire_active = true;
        }

        else if (!fire && fire_active) {

            ESP_LOGI(TAG, "FIRE CLEARED");

            send_reset_alarm();

            fire_active = false;
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
static void send_fire_alarm(void) ///
{   
    if (send_fire_alarm_flag) {
        ESP_LOGI(TAG, "FIRE ALARM already sent");
        return;
    }

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
    common.ctx.send_ttl = 3;
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
        ESP_LOGI(TAG, "FIRE ALARM SENT");
    }
    
}
static void send_reset_alarm(void) ///
{   
    if (send_reset_alarm_flag) {
        ESP_LOGI(TAG, "RESET ALARM already sent");
        return;
    }

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

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    } 
       mq2_init();
}
