/* VENDOR_CLIENT/main.c - Application main entry point */


#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"
/////////////

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "driver/gpio.h" 

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_timer.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"////
#include "esp_ble_mesh_local_data_operation_api.h"

#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"
#include "board.h"

#define TAG "EXAMPLE"

#define CID_ESP             0x02E5

#define PROV_OWN_ADDR       0x0001

#define MSG_SEND_TTL        3
#define MSG_TIMEOUT         0
#define MSG_ROLE            ROLE_PROVISIONER

#define COMP_DATA_PAGE_0    0x00

#define APP_KEY_IDX         0x0000
#define APP_KEY_OCTET       0x12

#define COMP_DATA_1_OCTET(msg, offset)      ((uint8_t)(msg[offset]))
#define COMP_DATA_2_OCTET(msg, offset)      (msg[offset + 1] << 8 | msg[offset])

#define ESP_BLE_MESH_VND_MODEL_ID_CLIENT    0x0002
#define ESP_BLE_MESH_VND_MODEL_ID_SERVER    0x0001

#define ESP_BLE_MESH_VND_MODEL_OP_SEND      ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

#define GROUP_ADDR_VENDOR 0xC000
#define GROUP_ADDR_ONOFF  0xC000
#define button_test_send GPIO_NUM_26
#define button_test_send_onoff GPIO_NUM_25
static bool ble_ready = false;
static uint8_t sub_count = 0;
static bool vendor_bind_done = false;
static bool onoff_bind_done = false;
static bool vendor_sub_done = false;
static bool onoff_sub_done = false;
typedef struct __attribute__((packed)) {///

    uint8_t  cmd;
    uint16_t mq2_adc;
    int16_t  temp_x10;
    int16_t  humi_x10;
    uint8_t  fire_level;
    //uint16_t  tid;
} fire_msg_t;
///////
static bool wifi_ready = false;
static bool mqtt_ready = false;
typedef struct {
    bool is_onoff;          // Flag để phân biệt tin nhắn Vendor hay OnOff
    uint16_t addr;
    fire_msg_t msg;
} mqtt_event_t;

typedef struct {
    bool has_vendor_server;
    bool has_onoff_server;
    bool has_onoff_client;
} node_feature_t;

static node_feature_t node_feature[0x100];

static void wifi_event_handler(void* arg,
                              esp_event_base_t event_base,
                              int32_t event_id,
                              void* event_data);

static void mqtt_event_handler(void *handler_args,
                              esp_event_base_t base,
                              int32_t event_id,
                              void *event_data);

static void mqtt_init(void);
static QueueHandle_t mqtt_queue;
static esp_mqtt_client_handle_t mqtt_client;

static const char *MQTT_URI =
    "YOUR_MQTT_URI";

static const char *MQTT_USER = "YOUR_MQTT_USER";
static const char *MQTT_PASS = "YOUR_MQTT_PASS";

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASS"

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "WIFI INIT DONE, CONNECTING...");
}
static void mqtt_init(void)
{
    extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_URI,
        .broker.verification.certificate = (const char *)ca_pem_start,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
    };

    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client,
                                    ESP_EVENT_ANY_ID,
                                    mqtt_event_handler,
                                    NULL);
    esp_mqtt_client_start(mqtt_client);

    ESP_LOGI(TAG, "MQTT STARTED");
}
///////
static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN];
static bool vendor_state_valid = false;
static fire_msg_t last_vendor_state;
static bool mesh_ready = false;
static bool onoff_ready = false;
static uint8_t onoff_tid = 0;

static struct example_info_store {
    uint16_t server_addr;   /* Vendor server unicast address */
    uint16_t vnd_tid;       /* TID contained in the vendor message */
} store = {
    .server_addr = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .vnd_tid = 0,
};

static nvs_handle_t NVS_HANDLE;
static const char * NVS_KEY = "vendor_client";

static struct esp_ble_mesh_key {
    uint16_t net_idx;
    uint16_t app_idx;
    uint8_t  app_key[ESP_BLE_MESH_OCTET16_LEN];
} prov_key;

static esp_ble_mesh_cfg_srv_t config_server = {
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
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

static esp_ble_mesh_client_t onoff_client;
static esp_ble_mesh_client_t config_client;

static const esp_ble_mesh_client_op_pair_t vnd_op_pair[] = {
    { ESP_BLE_MESH_VND_MODEL_OP_SEND, ESP_BLE_MESH_VND_MODEL_OP_STATUS },
};

static esp_ble_mesh_client_t vendor_client = {
    .op_pair_size = ARRAY_SIZE(vnd_op_pair),
    .op_pair = vnd_op_pair,
};

static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_STATUS, 6),
    //ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SEND, sizeof(fire_msg_t)),
    ESP_BLE_MESH_MODEL_OP_END,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 1, ROLE_NODE);
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub,&onoff_client),
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, ESP_BLE_MESH_VND_MODEL_ID_CLIENT,
    vnd_op, NULL, &vendor_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .prov_uuid          = dev_uuid,
    .prov_unicast_addr  = PROV_OWN_ADDR,
    .prov_start_address = 0x0005,
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
        ESP_LOGI(TAG, "Restore, server_addr 0x%04x, vnd_tid 0x%04x", store.server_addr, store.vnd_tid);
    }
}

static void example_ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                            esp_ble_mesh_node_t *node,
                                            esp_ble_mesh_model_t *model, uint32_t opcode)
{
    common->opcode = opcode;
    common->model = model;
    common->ctx.net_idx = prov_key.net_idx;
    common->ctx.app_idx = prov_key.app_idx;
    common->ctx.addr = node->unicast_addr;
    common->ctx.send_ttl = MSG_SEND_TTL;
    common->msg_timeout = MSG_TIMEOUT;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
    common->msg_role = MSG_ROLE;
#endif
}

static esp_err_t prov_complete(uint16_t node_index, const esp_ble_mesh_octet16_t uuid,
                               uint16_t primary_addr, uint8_t element_num, uint16_t net_idx)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_get_state_t get = {0};
    esp_ble_mesh_node_t *node = NULL;
    char name[10] = {'\0'};
    esp_err_t err;

    ESP_LOGI(TAG, "node_index %u, primary_addr 0x%04x, element_num %u, net_idx 0x%03x",
        node_index, primary_addr, element_num, net_idx);

    store.server_addr = primary_addr;
    mesh_example_info_store(); /* Store proper mesh example info */

    sprintf(name, "%s%02x", "NODE-", node_index);
    err = esp_ble_mesh_provisioner_set_node_name(node_index, name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set node name");
        return ESP_FAIL;
    }

    node = esp_ble_mesh_provisioner_get_node_with_addr(primary_addr);
    if (node == NULL) {
        ESP_LOGE(TAG, "Failed to get node 0x%04x info", primary_addr);
        return ESP_FAIL;
    }

    example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
    get.comp_data_get.page = COMP_DATA_PAGE_0;
    err = esp_ble_mesh_config_client_get_state(&common, &get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send Config Composition Data Get");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void recv_unprov_adv_pkt(uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN], uint8_t addr[BD_ADDR_LEN],
                                esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                                uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer)
{
    esp_ble_mesh_unprov_dev_add_t add_dev = {0};
    esp_err_t err;

    /* Due to the API esp_ble_mesh_provisioner_set_dev_uuid_match, Provisioner will only
     * use this callback to report the devices, whose device UUID starts with 0xdd & 0xdd,
     * to the application layer.
     */

    ESP_LOGI(TAG, "Address type 0x%02x, adv type 0x%02x", addr_type, adv_type);
    ESP_LOGI(TAG, "oob info 0x%04x, bearer %s", oob_info, (bearer & ESP_BLE_MESH_PROV_ADV) ? "PB-ADV" : "PB-GATT");

    memcpy(add_dev.addr, addr, BD_ADDR_LEN);
    add_dev.addr_type = (esp_ble_mesh_addr_type_t)addr_type;
    memcpy(add_dev.uuid, dev_uuid, ESP_BLE_MESH_OCTET16_LEN);
    add_dev.oob_info = oob_info;
    add_dev.bearer = (esp_ble_mesh_prov_bearer_t)bearer;
    /* Note: If unprovisioned device adv packets have not been received, we should not add
             device with ADD_DEV_START_PROV_NOW_FLAG set. */
    err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev,
            ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning device");
    }
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        //mesh_example_info_restore(); /* Restore proper mesh example info */
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT, err_code %d", param->provisioner_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT, err_code %d", param->provisioner_prov_disable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT");
        recv_unprov_adv_pkt(param->provisioner_recv_unprov_adv_pkt.dev_uuid, param->provisioner_recv_unprov_adv_pkt.addr,
                            param->provisioner_recv_unprov_adv_pkt.addr_type, param->provisioner_recv_unprov_adv_pkt.oob_info,
                            param->provisioner_recv_unprov_adv_pkt.adv_type, param->provisioner_recv_unprov_adv_pkt.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT, bearer %s",
            param->provisioner_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT, bearer %s, reason 0x%02x",
            param->provisioner_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", param->provisioner_prov_link_close.reason);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        ble_ready = true;
        prov_complete(param->provisioner_prov_complete.node_idx, param->provisioner_prov_complete.device_uuid,
                      param->provisioner_prov_complete.unicast_addr, param->provisioner_prov_complete.element_num,
                      param->provisioner_prov_complete.netkey_idx);

        mesh_example_info_restore(); //new restore, after ignore the store on ESP_BLE_MESH_PROV_REGISTER_COMP_EVT
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT, err_code %d", param->provisioner_add_unprov_dev_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT, err_code %d", param->provisioner_set_dev_uuid_match_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT, err_code %d", param->provisioner_set_node_name_comp.err_code);
        if (param->provisioner_set_node_name_comp.err_code == 0) {
            const char *name = esp_ble_mesh_provisioner_get_node_name(param->provisioner_set_node_name_comp.node_index);
            if (name) {
                ESP_LOGI(TAG, "Node %d name %s", param->provisioner_set_node_name_comp.node_index, name);
            }
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT, err_code %d", param->provisioner_add_app_key_comp.err_code);
        if (param->provisioner_add_app_key_comp.err_code == 0) {
            prov_key.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
            prov_key.app_idx = param->provisioner_add_app_key_comp.app_idx;
            esp_err_t err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, prov_key.app_idx,
                    ESP_BLE_MESH_VND_MODEL_ID_CLIENT, CID_ESP);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to bind AppKey to vendor client");
            }
            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR,///
                prov_key.app_idx,
                ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI,
                ESP_BLE_MESH_CID_NVAL);

        if (err != ESP_OK) {///
            ESP_LOGE(TAG,
                "Failed to bind AppKey to OnOff client");
        }
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT, err_code %d", param->provisioner_bind_app_key_to_model_comp.err_code);
        if (param->provisioner_bind_app_key_to_model_comp.err_code == 0) {
            ESP_LOGI(TAG, "ONOFF MODEL READY");
            onoff_ready = true;
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT, err_code %d", param->provisioner_store_node_comp_data_comp.err_code);
        break;
    default:
        break;
    }
}

static void example_ble_mesh_parse_node_comp_data( uint16_t addr, const uint8_t *data, uint16_t length)
{   
    bool has_vendor_server = false;
    bool has_onoff_server = false;
    bool has_onoff_client = false;
    uint16_t cid, pid, vid, crpl, feat;
    uint16_t loc, model_id, company_id;
    uint8_t nums, numv;
    uint16_t offset;
    int i;

    cid = COMP_DATA_2_OCTET(data, 0);
    pid = COMP_DATA_2_OCTET(data, 2);
    vid = COMP_DATA_2_OCTET(data, 4);
    crpl = COMP_DATA_2_OCTET(data, 6);
    feat = COMP_DATA_2_OCTET(data, 8);
    offset = 10;

    ESP_LOGI(TAG, "********************** Composition Data Start **********************");
    ESP_LOGI(TAG, "* CID 0x%04x, PID 0x%04x, VID 0x%04x, CRPL 0x%04x, Features 0x%04x *", cid, pid, vid, crpl, feat);
    for (; offset < length; ) {
        loc = COMP_DATA_2_OCTET(data, offset);
        nums = COMP_DATA_1_OCTET(data, offset + 2);
        numv = COMP_DATA_1_OCTET(data, offset + 3);
        offset += 4;
        ESP_LOGI(TAG, "* Loc 0x%04x, NumS 0x%02x, NumV 0x%02x *", loc, nums, numv);
        for (i = 0; i < nums; i++) {
            model_id = COMP_DATA_2_OCTET(data, offset);
            ESP_LOGI(TAG, "* SIG Model ID 0x%04x *", model_id);
                if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
                    has_onoff_server = true;
                }
                if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
                    has_onoff_client = true;
                }
            offset += 2;
        }
        for (i = 0; i < numv; i++) {
            company_id = COMP_DATA_2_OCTET(data, offset);
            model_id = COMP_DATA_2_OCTET(data, offset + 2);
            ESP_LOGI(TAG, "* Vendor Model ID 0x%04x, Company ID 0x%04x *", model_id, company_id);
            if (company_id == CID_ESP &&
                model_id == ESP_BLE_MESH_VND_MODEL_ID_SERVER) {
                has_vendor_server = true;
            }
            offset += 4;
        }
    }
    node_feature[addr].has_vendor_server = has_vendor_server;
    node_feature[addr].has_onoff_server = has_onoff_server;
    node_feature[addr].has_onoff_client = has_onoff_client;
    ESP_LOGW(TAG,
    "NODE 0x%04X | vendor=%d onoff_srv=%d onoff_cli=%d",
    addr,
    has_vendor_server,
    has_onoff_server,
    has_onoff_client);
    ESP_LOGI(TAG, "*********************** Composition Data End ***********************");
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
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
        param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {

        uint8_t new_state = param->status_cb.onoff_status.present_onoff;
        ESP_LOGI(TAG, "Update onoff = %d", new_state);

        // Chỉ gửi MQTT nếu chân 26 đang được nối kín (mức 0)
        if (gpio_get_level(button_test_send) == 0) {
            mqtt_event_t evt = {0};
            evt.is_onoff = true;
            evt.addr = param->params->ctx.addr;
            evt.msg.cmd = new_state; // Tận dụng field cmd để chứa trạng thái 0/1
            xQueueSend(mqtt_queue, &evt, 0);
        }

        mesh_example_info_store();   // SAVE STATE
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
static void example_ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                                              esp_ble_mesh_cfg_client_cb_param_t *param)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_set_state_t set = {0};
    esp_ble_mesh_node_t *node = NULL;
    esp_err_t err;

    ESP_LOGI(TAG, "Config client, err_code %d, event %u, addr 0x%04x, opcode 0x%04" PRIx32,
        param->error_code, event, param->params->ctx.addr, param->params->opcode);

    if (param->error_code) {
        ESP_LOGE(TAG, "Send config client message failed, opcode 0x%04" PRIx32, param->params->opcode);
        return;
    }

    node = esp_ble_mesh_provisioner_get_node_with_addr(param->params->ctx.addr);
    if (!node) {
        ESP_LOGE(TAG, "Failed to get node 0x%04x info", param->params->ctx.addr);
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
                param->status_cb.comp_data_status.composition_data->len);
            example_ble_mesh_parse_node_comp_data(param->params->ctx.addr,param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            err = esp_ble_mesh_provisioner_store_node_comp_data(param->params->ctx.addr,
                param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store node composition data");
                break;
            }

            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set.app_key_add.net_idx = prov_key.net_idx;
            set.app_key_add.app_idx = prov_key.app_idx;
            memcpy(set.app_key_add.app_key, prov_key.app_key, ESP_BLE_MESH_OCTET16_LEN);
            err = esp_ble_mesh_config_client_set_state(&common, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config AppKey Add");
            }
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
            uint16_t addr = node->unicast_addr;
            // BƯỚC 1: Bắt đầu chuỗi cấu hình bằng cách Bind Vendor Server trước (nếu có)
            if (node_feature[addr].has_vendor_server) {
                ESP_LOGI(TAG, "Bind Vendor Server");
                example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                set.model_app_bind.element_addr = addr;
                set.model_app_bind.model_app_idx = prov_key.app_idx;
                set.model_app_bind.model_id = ESP_BLE_MESH_VND_MODEL_ID_SERVER;
                set.model_app_bind.company_id = CID_ESP;
                esp_ble_mesh_config_client_set_state(&common, &set);
            } else if (node_feature[addr].has_onoff_server) {
                // Nếu không có Vendor, thử Bind OnOff luôn
                ESP_LOGI(TAG, "Bind OnOff Server");
                example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                set.model_app_bind.element_addr = addr;
                set.model_app_bind.model_app_idx = prov_key.app_idx;
                set.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV;
                set.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                esp_ble_mesh_config_client_set_state(&common, &set);
            } else if (node_feature[addr].has_onoff_client) {
                ESP_LOGI(TAG, "Bind OnOff Client");
                example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                set.model_app_bind.element_addr = addr;
                set.model_app_bind.model_app_idx = prov_key.app_idx;
                set.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI;
                set.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                esp_ble_mesh_config_client_set_state(&common, &set);
            }
        } else if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            uint16_t model_id = param->status_cb.model_app_status.model_id;
            uint16_t addr = param->params->ctx.addr;

            if (model_id == ESP_BLE_MESH_VND_MODEL_ID_SERVER) {
                ESP_LOGI(TAG, "Vendor Bind Done -> Subscribing...");
                example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD);
                set.model_sub_add.element_addr = addr;
                set.model_sub_add.sub_addr = GROUP_ADDR_VENDOR;
                set.model_sub_add.model_id = ESP_BLE_MESH_VND_MODEL_ID_SERVER;
                set.model_sub_add.company_id = CID_ESP;
                esp_ble_mesh_config_client_set_state(&common, &set);
            } else if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
                ESP_LOGI(TAG, "OnOff Bind Done -> Subscribing...");
                example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD);
                set.model_sub_add.element_addr = addr;
                set.model_sub_add.sub_addr = GROUP_ADDR_ONOFF;
                set.model_sub_add.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV;
                set.model_sub_add.company_id = ESP_BLE_MESH_CID_NVAL;
                esp_ble_mesh_config_client_set_state(&common, &set);
            } else if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
                ESP_LOGI(TAG, "OnOff Client Bind Done -> Setting Publication...");
                example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET);
                set.model_pub_set.element_addr = addr;
                set.model_pub_set.publish_addr = GROUP_ADDR_ONOFF;
                set.model_pub_set.publish_app_idx = prov_key.app_idx;
                set.model_pub_set.publish_ttl = 7;
                set.model_pub_set.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI;
                set.model_pub_set.company_id = ESP_BLE_MESH_CID_NVAL;
                esp_ble_mesh_config_client_set_state(&common, &set);
            }
        } else if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD) {
            uint16_t model_id = param->status_cb.model_sub_status.model_id;
            uint16_t addr = param->params->ctx.addr;

            if (model_id == ESP_BLE_MESH_VND_MODEL_ID_SERVER) {
                if (node_feature[addr].has_onoff_server) {
                    ESP_LOGI(TAG, "Now Binding OnOff Server...");
                    example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                    set.model_app_bind.element_addr = addr;
                    set.model_app_bind.model_app_idx = prov_key.app_idx;
                    set.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV;
                    set.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                    esp_ble_mesh_config_client_set_state(&common, &set);
                } else if (node_feature[addr].has_onoff_client) {
                    ESP_LOGI(TAG, "Now Binding OnOff Client...");
                    example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                    set.model_app_bind.element_addr = addr;
                    set.model_app_bind.model_app_idx = prov_key.app_idx;
                    set.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI;
                    set.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                    esp_ble_mesh_config_client_set_state(&common, &set);
                } else {
                    mesh_ready = true;
                }
            } else if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
                if (node_feature[addr].has_onoff_client) {
                    ESP_LOGI(TAG, "Now Binding OnOff Client...");
                    example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                    set.model_app_bind.element_addr = addr;
                    set.model_app_bind.model_app_idx = prov_key.app_idx;
                    set.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI;
                    set.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                    esp_ble_mesh_config_client_set_state(&common, &set);
                } else {
                    mesh_ready = true;
                }
            }
        } else if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET) {
            ESP_LOGW(TAG, "OnOff Client Publication Set Done. Node Ready!");
            mesh_ready = true;
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_STATUS) {
                param->status_cb.comp_data_status.composition_data->len);
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        switch (param->params->opcode) {
        case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET: {
            esp_ble_mesh_cfg_client_get_state_t get = {0};
            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
            get.comp_data_get.page = COMP_DATA_PAGE_0;
            err = esp_ble_mesh_config_client_get_state(&common, &get);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config Composition Data Get");
            }
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set.app_key_add.net_idx = prov_key.net_idx;
            set.app_key_add.app_idx = prov_key.app_idx;
            memcpy(set.app_key_add.app_key, prov_key.app_key, ESP_BLE_MESH_OCTET16_LEN);
            err = esp_ble_mesh_config_client_set_state(&common, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config AppKey Add");
            }
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            set.model_app_bind.element_addr = node->unicast_addr;
            set.model_app_bind.model_app_idx = prov_key.app_idx;
            set.model_app_bind.model_id = ESP_BLE_MESH_VND_MODEL_ID_SERVER;
            set.model_app_bind.company_id = CID_ESP;
            err = esp_ble_mesh_config_client_set_state(&common, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config Model App Bind");
            }
            break;
        default:
            break;
        }
        break;
    default:
        ESP_LOGE(TAG, "Invalid config client event %u", event);
        break;
    }
}

void example_ble_mesh_send_vendor_message(bool resend)
{
    esp_ble_mesh_msg_ctx_t ctx = {0};
    esp_err_t err;

    if (!resend) {
        store.vnd_tid++;
    }

    uint8_t msg[3];

    msg[0] = 0x10;
    msg[1] = (store.vnd_tid >> 8) & 0xFF;
    msg[2] = store.vnd_tid & 0xFF;

    ctx.net_idx = prov_key.net_idx;
    ctx.app_idx = prov_key.app_idx;
    ctx.addr = GROUP_ADDR_VENDOR; //(store.server_addr == 0x0000 ) ? 0xC000 :store.server_addr;
    ctx.send_ttl = MSG_SEND_TTL;

    ESP_LOGI(TAG,
        "GET SENSOR STATE -> 0x%04X",
        ctx.addr);

    err = esp_ble_mesh_client_model_send_msg(
        vendor_client.model,
        &ctx,
        ESP_BLE_MESH_VND_MODEL_OP_SEND,
        sizeof(msg),
        msg,
        MSG_TIMEOUT,
        true,
        MSG_ROLE);

    if (err != ESP_OK) {
        ESP_LOGE(TAG,
            "SEND FAIL: %s",
            esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG,
        "GET SENT VENDOR MESSAGE | TID=0x%04X",
        store.vnd_tid);
}

static uint8_t tid;

void send_onoff(uint16_t addr, uint8_t onoff)
{
    esp_ble_mesh_client_common_param_t param = {0};
    static esp_ble_mesh_model_t *onoff_cli_model =&root_models[2];
    param.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;
    param.model = onoff_cli_model;   
    param.ctx.net_idx = prov_key.net_idx;
    param.ctx.app_idx = prov_key.app_idx;
    param.ctx.addr = addr;
    param.ctx.send_ttl = 3;

    esp_ble_mesh_generic_client_set_state_t set = {0};

    set.onoff_set.op_en = false;
    set.onoff_set.onoff = onoff;
    set.onoff_set.tid = tid++;

    esp_ble_mesh_generic_client_set_state(&param, &set);
    ESP_LOGI("TEST", "model=%p, app_idx= %d, net_idx=%d", param.model, param.ctx.app_idx,param.ctx.net_idx);
}
static void example_ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                             esp_ble_mesh_model_cb_param_t *param)
{
    static int64_t start_time;

    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_STATUS) {
            ESP_LOGW(TAG, "Received Vendor Status from Server (Addr: 0x%04x, Len: %d)", 
                     param->model_operation.ctx->addr, param->model_operation.length);
            /*int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Recv 0x06%" PRIx32 ", tid 0x%04x, time %lldus",
                param->model_operation.opcode, store.vnd_tid, end_time - start_time);*/
        fire_msg_t *msg = (fire_msg_t *)param->model_operation.msg;

        ESP_LOGW(TAG,"FIRE ALERT RECEIVED");

        ESP_LOGI(TAG,
            "MQ2=%d TEMP=%.1f HUMI=%.1f FIRE=%d",
            msg->mq2_adc,
            msg->temp_x10 / 10.0,
            msg->humi_x10 / 10.0,
            msg->fire_level);

        if (msg->fire_level >= 2) {
            ESP_LOGE(TAG,"!!! FIRE DETECTED !!!");
            send_onoff(GROUP_ADDR_ONOFF, 1);
        }      

        // Chỉ gửi MQTT nếu chân 26 đang được nối kín (mức 0)
        if (gpio_get_level(button_test_send) == 0) {
            mqtt_event_t evt;
            evt.is_onoff = false;
            evt.addr = param->model_operation.ctx->addr;
            memcpy(&evt.msg, msg, sizeof(fire_msg_t));
            if (xQueueSend(mqtt_queue, &evt, pdMS_TO_TICKS(10)) != pdTRUE) {
                ESP_LOGW(TAG, "MQTT queue full, drop packet");
            }
        }

        }
        break;
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG, "Failed to send message 0x%06" PRIx32, param->model_send_comp.opcode);
            break;
        }
        start_time = esp_timer_get_time();
        ESP_LOGI(TAG, "Send 0x%06" PRIx32, param->model_send_comp.opcode);
        break;
case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:

    ESP_LOGI(TAG, "RX len = %d",
        param->client_recv_publish_msg.length);

    ESP_LOGI(TAG, "Receive publish message 0x%06" PRIx32,
        param->client_recv_publish_msg.opcode);

    if (param->client_recv_publish_msg.opcode == ESP_BLE_MESH_VND_MODEL_OP_STATUS) {
        if (param->client_recv_publish_msg.length != sizeof(fire_msg_t)) {
            ESP_LOGW(TAG, "Invalid STATUS length = %d",
                param->client_recv_publish_msg.length);
            break;
        } 
        fire_msg_t *msg = (fire_msg_t *)param->client_recv_publish_msg.msg;
        memcpy(&last_vendor_state, msg, sizeof(fire_msg_t));
        vendor_state_valid = true;

        ESP_LOGI(TAG, "Sensor Data -> MQ2:%d T:%.1f H:%.1f Fire:%d", 
                 msg->mq2_adc, msg->temp_x10/10.0, msg->humi_x10/10.0, msg->fire_level);

        if (msg->fire_level >= 2) {
            ESP_LOGE(TAG, "!!! FIRE DETECTED -> Sending Alarm ON !!!");
            send_onoff(GROUP_ADDR_ONOFF, 1); // Chỉ gửi lệnh ON (1)
        }

            // Chỉ gửi MQTT nếu chân 26 đang được nối kín (mức 0)
            if (gpio_get_level(button_test_send) == 0) {
                mqtt_event_t evt;
                evt.is_onoff = false;
                evt.addr = 0x0000; // nếu bạn không có addr thì giữ 0
                memcpy(&evt.msg, msg, sizeof(fire_msg_t));
                if (xQueueSend(mqtt_queue, &evt, pdMS_TO_TICKS(10)) != pdTRUE) {
                    ESP_LOGW(TAG, "MQTT queue full, drop packet");
                }
            }
    }
    break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Client message 0x%06" PRIx32 " timeout", param->client_send_timeout.opcode);
        example_ble_mesh_send_vendor_message(true);
        break;
    default:
        break;
    }
}
static void wifi_event_handler(void* arg,
                              esp_event_base_t event_base,
                              int32_t event_id,
                              void* event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        }

        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_ready = false;
            ESP_LOGW(TAG, "WIFI DISCONNECTED → reconnect");
            esp_wifi_connect();
        }
    }

    if (event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP) {

        wifi_ready = true;
        ESP_LOGI(TAG, "WIFI READY (GOT IP)");
    }
}

static void mqtt_event_handler(void *handler_args,
                              esp_event_base_t base,
                              int32_t event_id,
                              void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        mqtt_ready = true;
        ESP_LOGI(TAG, "MQTT READY");
        break;

    case MQTT_EVENT_DISCONNECTED:
        mqtt_ready = false;
        ESP_LOGW(TAG, "MQTT LOST");
        break;

    default:
        break;
    }
}

static esp_err_t ble_mesh_init(void)
{
    uint8_t match[2] = { 0x32, 0x10 };
    esp_err_t err;

    prov_key.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    prov_key.app_idx = APP_KEY_IDX;
    memset(prov_key.app_key, APP_KEY_OCTET, sizeof(prov_key.app_key));

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_client_callback(example_ble_mesh_config_client_cb);
    esp_ble_mesh_register_custom_model_callback(example_ble_mesh_custom_model_cb);
    esp_ble_mesh_register_generic_client_callback(example_ble_mesh_generic_client_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return err;
    }

    err = esp_ble_mesh_client_model_init(&vnd_models[0]);
    if (err) {
        ESP_LOGE(TAG, "Failed to initialize vendor client");
        return err;
    }
    vendor_client.model = &vnd_models[0];
    
    /* Hiện tại đã bỏ UUID match để bạn dễ quét mọi loại Node */
    
    // Sử dụng đúng hàm để Sub địa chỉ Group cho Model cục bộ trên Provisioner
    err = esp_ble_mesh_model_subscribe_group_addr(PROV_OWN_ADDR, ESP_BLE_MESH_CID_NVAL,
            ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI, GROUP_ADDR_ONOFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe group addr for local OnOff client (0x%x)", err);
    }

    if (onoff_client.model == NULL) {
        ESP_LOGW(TAG, "ONOFF model loading...");
    }

    err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh provisioner");
        return err;
    }

    err = esp_ble_mesh_provisioner_add_local_app_key(prov_key.app_key, prov_key.net_idx, prov_key.app_idx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add local AppKey");
        return err;
    }

    ESP_LOGI(TAG, "ESP BLE Mesh Provisioner initialized");

    return ESP_OK;
}

static void mqtt_bridge_task(void *arg)
{   
    mqtt_event_t evt;

    while (1) {
        if (!wifi_ready || !mqtt_ready) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (xQueueReceive(mqtt_queue, &evt, portMAX_DELAY)) {

            char topic[64];
            char payload[256];

            snprintf(topic, sizeof(topic),
                     "fire/alarm");

            if (evt.is_onoff) {
                snprintf(payload, sizeof(payload),
                         "{\"onoff\":%d}", evt.msg.cmd);
            } else {
                snprintf(payload, sizeof(payload),
                         "{\"mq2\":%d,\"temp\":%.1f,\"humi\":%.1f,\"fire\":%d}",
                         evt.msg.mq2_adc,
                         evt.msg.temp_x10 / 10.0,
                         evt.msg.humi_x10 / 10.0,
                         evt.msg.fire_level);
            }
            
            esp_mqtt_client_publish(
                mqtt_client,
                topic,
                payload,
                0, 1, 0
            );

            ESP_LOGI(TAG, "MQTT SENT: %s", payload);
        }
    }
}

void button_task(void *arg)
{
    ESP_LOGI(TAG, "Button task started");

    int last_vendor = 1;
    int last_onoff = 1;

    while (1) {
        int v = gpio_get_level(button_test_send);
        if (v == 0 && last_vendor == 1) {
            ESP_LOGI(TAG, "VENDOR BUTTON");
            example_ble_mesh_send_vendor_message(false);
        }
        last_vendor = v;
        int o = gpio_get_level(button_test_send_onoff);
        if (o == 0 && last_onoff == 1) {
            ESP_LOGI(TAG, "ONOFF BUTTON - SEND ON");
            send_onoff(GROUP_ADDR_ONOFF, 1); // Fix lỗi thiếu tham số và không toggle
        }
        last_onoff = o;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void polling_task(void *arg)
{
    ESP_LOGI(TAG, "Polling task started");
    while (1) {
        if(gpio_get_level(button_test_send)==0)
        {
            example_ble_mesh_send_vendor_message(false);
            
        }vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
void app_main(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << button_test_send | 1ULL << button_test_send_onoff),
        .pull_down_en = 0,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };

    gpio_config(&io_conf);

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
    if (err != ESP_OK) {
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
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }
    wifi_init();
    mqtt_init();
    mqtt_queue = xQueueCreate(10, sizeof(mqtt_event_t));
    xTaskCreate(mqtt_bridge_task, "mqtt_bridge_task", 4096, NULL, 5, NULL);
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
    xTaskCreate(polling_task, "polling_task", 2048, NULL, 5, NULL);
}
