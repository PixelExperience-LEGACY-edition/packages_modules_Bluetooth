/******************************************************************************
 *
 *  Copyright (c) 2014 The Android Open Source Project
 *  Copyright 2009-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/*******************************************************************************
 *
 *  Filename:      btif_storage.c
 *
 *  Description:   Stores the local BT adapter and remote device properties in
 *                 NVRAM storage, typically as xml file in the
 *                 mobile's filesystem
 *
 *
 */

#define LOG_TAG "bt_btif_storage"

#include "btif_storage.h"

#include <alloca.h>
#include <base/logging.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "bta_csis_api.h"
#include "bta_groups.h"
#include "bta_has_api.h"
#include "bta_hd_api.h"
#include "bta_hearing_aid_api.h"
#include "bta_hh_api.h"
#include "bta_le_audio_api.h"
#include "btif_api.h"
#include "btif_config.h"
#include "btif_hd.h"
#include "btif_hh.h"
#include "btif_util.h"
#include "device/include/controller.h"
#include "gd/common/init_flags.h"
#include "osi/include/allocator.h"
#include "osi/include/compat.h"
#include "osi/include/config.h"
#include "osi/include/log.h"
#include "osi/include/osi.h"
#include "stack/include/bt_octets.h"
#include "stack/include/btu.h"
#include "types/bluetooth/uuid.h"
#include "types/raw_address.h"

using base::Bind;
using bluetooth::Uuid;
using bluetooth::csis::CsisClient;
using bluetooth::groups::DeviceGroups;

/*******************************************************************************
 *  Constants & Macros
 ******************************************************************************/

// TODO(armansito): Find a better way than using a hardcoded path.
#define BTIF_STORAGE_PATH_BLUEDROID "/data/misc/bluedroid"

//#define BTIF_STORAGE_PATH_ADAPTER_INFO "adapter_info"
//#define BTIF_STORAGE_PATH_REMOTE_DEVICES "remote_devices"
#define BTIF_STORAGE_PATH_REMOTE_DEVTIME "Timestamp"
#define BTIF_STORAGE_PATH_REMOTE_DEVCLASS "DevClass"
#define BTIF_STORAGE_PATH_REMOTE_DEVTYPE "DevType"
#define BTIF_STORAGE_PATH_REMOTE_NAME "Name"

//#define BTIF_STORAGE_PATH_REMOTE_LINKKEYS "remote_linkkeys"
#define BTIF_STORAGE_PATH_REMOTE_ALIASE "Aliase"
#define BTIF_STORAGE_PATH_REMOTE_SERVICE "Service"
#define BTIF_STORAGE_PATH_REMOTE_HIDINFO "HidInfo"
#define BTIF_STORAGE_KEY_ADAPTER_NAME "Name"
#define BTIF_STORAGE_KEY_ADAPTER_SCANMODE "ScanMode"
#define BTIF_STORAGE_KEY_LOCAL_IO_CAPS "LocalIOCaps"
#define BTIF_STORAGE_KEY_LOCAL_IO_CAPS_BLE "LocalIOCapsBLE"
#define BTIF_STORAGE_KEY_MAX_SESSION_KEY_SIZE "MaxSessionKeySize"
#define BTIF_STORAGE_KEY_ADAPTER_DISC_TIMEOUT "DiscoveryTimeout"
#define BTIF_STORAGE_KEY_GATT_CLIENT_SUPPORTED "GattClientSupportedFeatures"
#define BTIF_STORAGE_KEY_GATT_CLIENT_DB_HASH "GattClientDatabaseHash"
#define BTIF_STORAGE_KEY_GATT_SERVER_SUPPORTED "GattServerSupportedFeatures"
#define BTIF_STORAGE_KEY_SECURE_CONNECTIONS_SUPPORTED \
  "SecureConnectionsSupported"
#define BTIF_STORAGE_DEVICE_GROUP_BIN "DeviceGroupBin"
#define BTIF_STORAGE_CSIS_AUTOCONNECT "CsisAutoconnect"
#define BTIF_STORAGE_CSIS_SET_INFO_BIN "CsisSetInfoBin"
#define BTIF_STORAGE_LEAUDIO_AUTOCONNECT "LeAudioAutoconnect"
#define BTIF_STORAGE_LEAUDIO_HANDLES_BIN "LeAudioHandlesBin"
#define BTIF_STORAGE_LEAUDIO_SINK_PACS_BIN "SinkPacsBin"
#define BTIF_STORAGE_LEAUDIO_SOURCE_PACS_BIN "SourcePacsBin"
#define BTIF_STORAGE_LEAUDIO_ASES_BIN "AsesBin"
#define BTIF_STORAGE_LEAUDIO_SINK_AUDIOLOCATION "SinkAudioLocation"
#define BTIF_STORAGE_LEAUDIO_SOURCE_AUDIOLOCATION "SourceAudioLocation"
#define BTIF_STORAGE_LEAUDIO_SINK_SUPPORTED_CONTEXT_TYPE \
  "SinkSupportedContextType"
#define BTIF_STORAGE_LEAUDIO_SOURCE_SUPPORTED_CONTEXT_TYPE \
  "SourceSupportedContextType"

#define BTIF_STORAGE_KEY_HID_RECONNECT_ALLOWED "HidReConnectAllowed"

/* This is a local property to add a device found */
#define BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP 0xFF

// TODO: This macro should be converted to a function
#define BTIF_STORAGE_GET_ADAPTER_PROP(s, t, v, l, p) \
  do {                                               \
    (p).type = (t);                                  \
    (p).val = (v);                                   \
    (p).len = (l);                                   \
    (s) = btif_storage_get_adapter_property(&(p));   \
  } while (0)

// TODO: This macro should be converted to a function
#define BTIF_STORAGE_GET_REMOTE_PROP(b, t, v, l, p)     \
  do {                                                  \
    (p).type = (t);                                     \
    (p).val = (v);                                      \
    (p).len = (l);                                      \
    btif_storage_get_remote_device_property((b), &(p)); \
  } while (0)

#define STORAGE_BDADDR_STRING_SZ (18) /* 00:11:22:33:44:55 */
#define STORAGE_UUID_STRING_SIZE \
  (36 + 1) /* 00001200-0000-1000-8000-00805f9b34fb; */
#define STORAGE_PINLEN_STRING_MAX_SIZE (2)  /* ascii pinlen max chars */
#define STORAGE_KEYTYPE_STRING_MAX_SIZE (1) /* ascii keytype max chars */

#define STORAGE_KEY_TYPE_MAX (10)

#define STORAGE_HID_ATRR_MASK_SIZE (4)
#define STORAGE_HID_SUB_CLASS_SIZE (2)
#define STORAGE_HID_APP_ID_SIZE (2)
#define STORAGE_HID_VENDOR_ID_SIZE (4)
#define STORAGE_HID_PRODUCT_ID_SIZE (4)
#define STORAGE_HID_VERSION_SIZE (4)
#define STORAGE_HID_CTRY_CODE_SIZE (2)
#define STORAGE_HID_DESC_LEN_SIZE (4)
#define STORAGE_HID_DESC_MAX_SIZE (2 * 512)

/* <18 char bd addr> <space> LIST< <36 char uuid> <;> > <keytype (dec)> <pinlen>
 */
#define BTIF_REMOTE_SERVICES_ENTRY_SIZE_MAX      \
  (STORAGE_BDADDR_STRING_SZ + 1 +                \
   STORAGE_UUID_STRING_SIZE * BT_MAX_NUM_UUIDS + \
   STORAGE_PINLEN_STRING_MAX_SIZE + STORAGE_KEYTYPE_STRING_MAX_SIZE)

#define STORAGE_REMOTE_LINKKEYS_ENTRY_SIZE (LINK_KEY_LEN * 2 + 1 + 2 + 1 + 2)

/* <18 char bd addr> <space>LIST <attr_mask> <space> > <sub_class> <space>
   <app_id> <space>
                                <vendor_id> <space> > <product_id> <space>
   <version> <space>
                                <ctry_code> <space> > <desc_len> <space>
   <desc_list> <space> */
#define BTIF_HID_INFO_ENTRY_SIZE_MAX                                  \
  (STORAGE_BDADDR_STRING_SZ + 1 + STORAGE_HID_ATRR_MASK_SIZE + 1 +    \
   STORAGE_HID_SUB_CLASS_SIZE + 1 + STORAGE_HID_APP_ID_SIZE + 1 +     \
   STORAGE_HID_VENDOR_ID_SIZE + 1 + STORAGE_HID_PRODUCT_ID_SIZE + 1 + \
   STORAGE_HID_VERSION_SIZE + 1 + STORAGE_HID_CTRY_CODE_SIZE + 1 +    \
   STORAGE_HID_DESC_LEN_SIZE + 1 + STORAGE_HID_DESC_MAX_SIZE + 1)

/* currently remote services is the potentially largest entry */
#define BTIF_STORAGE_MAX_LINE_SZ BTIF_REMOTE_SERVICES_ENTRY_SIZE_MAX

/* check against unv max entry size at compile time */
#if (BTIF_STORAGE_ENTRY_MAX_SIZE > UNV_MAXLINE_LENGTH)
#error "btif storage entry size exceeds unv max line size"
#endif

/*******************************************************************************
 *  Local type definitions
 ******************************************************************************/
typedef struct {
  uint32_t num_devices;
  RawAddress devices[BTM_SEC_MAX_DEVICE_RECORDS];
} btif_bonded_devices_t;

/*******************************************************************************
 *  External functions
 ******************************************************************************/

extern void btif_gatts_add_bonded_dev_from_nv(const RawAddress& bda);

/*******************************************************************************
 *  Internal Functions
 ******************************************************************************/

static bt_status_t btif_in_fetch_bonded_ble_device(
    const std::string& remote_bd_addr, int add,
    btif_bonded_devices_t* p_bonded_devices);
static bt_status_t btif_in_fetch_bonded_device(const std::string& bdstr);

static bool btif_has_ble_keys(const std::string& bdstr);

/*******************************************************************************
 *  Static functions
 ******************************************************************************/

static int prop2cfg(const RawAddress* remote_bd_addr, bt_property_t* prop) {
  std::string bdstr;
  if (remote_bd_addr) {
    bdstr = remote_bd_addr->ToString();
  }

  char value[1024];
  if (prop->len <= 0 || prop->len > (int)sizeof(value) - 1) {
    LOG_WARN(
        "Unable to save property to configuration file type:%d, "
        " len:%d is invalid",
        prop->type, prop->len);
    return false;
  }
  switch (prop->type) {
    case BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP:
      btif_config_set_int(bdstr, BTIF_STORAGE_PATH_REMOTE_DEVTIME,
                          (int)time(NULL));
      break;
    case BT_PROPERTY_BDNAME: {
      int name_length = prop->len > BTM_MAX_LOC_BD_NAME_LEN
                            ? BTM_MAX_LOC_BD_NAME_LEN
                            : prop->len;
      strncpy(value, (char*)prop->val, name_length);
      value[name_length] = '\0';
      if (remote_bd_addr) {
        btif_config_set_str(bdstr, BTIF_STORAGE_PATH_REMOTE_NAME, value);
      } else {
        btif_config_set_str("Adapter", BTIF_STORAGE_KEY_ADAPTER_NAME, value);
        btif_config_flush();
      }
      break;
    }
    case BT_PROPERTY_REMOTE_FRIENDLY_NAME:
      strncpy(value, (char*)prop->val, prop->len);
      value[prop->len] = '\0';
      btif_config_set_str(bdstr, BTIF_STORAGE_PATH_REMOTE_ALIASE, value);
      break;
    case BT_PROPERTY_ADAPTER_SCAN_MODE:
      btif_config_set_int("Adapter", BTIF_STORAGE_KEY_ADAPTER_SCANMODE,
                          *(int*)prop->val);
      break;
    case BT_PROPERTY_LOCAL_IO_CAPS:
      btif_config_set_int("Adapter", BTIF_STORAGE_KEY_LOCAL_IO_CAPS,
                          *(int*)prop->val);
      break;
    case BT_PROPERTY_LOCAL_IO_CAPS_BLE:
      btif_config_set_int("Adapter", BTIF_STORAGE_KEY_LOCAL_IO_CAPS_BLE,
                          *(int*)prop->val);
      break;
    case BT_PROPERTY_ADAPTER_DISCOVERABLE_TIMEOUT:
      btif_config_set_int("Adapter", BTIF_STORAGE_KEY_ADAPTER_DISC_TIMEOUT,
                          *(int*)prop->val);
      break;
    case BT_PROPERTY_CLASS_OF_DEVICE:
      btif_config_set_int(bdstr, BTIF_STORAGE_PATH_REMOTE_DEVCLASS,
                          *(int*)prop->val);
      break;
    case BT_PROPERTY_TYPE_OF_DEVICE:
      btif_config_set_int(bdstr, BTIF_STORAGE_PATH_REMOTE_DEVTYPE,
                          *(int*)prop->val);
      break;
    case BT_PROPERTY_UUIDS: {
      std::string val;
      size_t cnt = (prop->len) / sizeof(Uuid);
      for (size_t i = 0; i < cnt; i++) {
        val += (reinterpret_cast<Uuid*>(prop->val) + i)->ToString() + " ";
      }
      btif_config_set_str(bdstr, BTIF_STORAGE_PATH_REMOTE_SERVICE, val);
      break;
    }
    case BT_PROPERTY_REMOTE_VERSION_INFO: {
      bt_remote_version_t* info = (bt_remote_version_t*)prop->val;

      if (!info) return false;

      btif_config_set_int(bdstr, BT_CONFIG_KEY_REMOTE_VER_MFCT,
                          info->manufacturer);
      btif_config_set_int(bdstr, BT_CONFIG_KEY_REMOTE_VER_VER, info->version);
      btif_config_set_int(bdstr, BT_CONFIG_KEY_REMOTE_VER_SUBVER,
                          info->sub_ver);
    } break;
    case BT_PROPERTY_REMOTE_SECURE_CONNECTIONS_SUPPORTED:
      btif_config_set_int(bdstr, BTIF_STORAGE_KEY_SECURE_CONNECTIONS_SUPPORTED,
                          *(uint8_t*)prop->val);
      break;
    case BT_PROPERTY_REMOTE_MAX_SESSION_KEY_SIZE:
      btif_config_set_int(bdstr, BTIF_STORAGE_KEY_MAX_SESSION_KEY_SIZE,
                          *(uint8_t*)prop->val);
      break;

    default:
      BTIF_TRACE_ERROR("Unknown prop type:%d", prop->type);
      return false;
  }

  /* No need to look for bonded device with address of NULL */
  if (remote_bd_addr &&
      btif_in_fetch_bonded_device(bdstr) == BT_STATUS_SUCCESS) {
    /* save changes if the device was bonded */
    btif_config_flush();
  }

  return true;
}

static int cfg2prop(const RawAddress* remote_bd_addr, bt_property_t* prop) {
  std::string bdstr;
  if (remote_bd_addr) {
    bdstr = remote_bd_addr->ToString();
  }
  if (prop->len <= 0) {
    LOG_WARN("Invalid property read from configuration file type:%d, len:%d",
             prop->type, prop->len);
    return false;
  }
  int ret = false;
  switch (prop->type) {
    case BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP:
      if (prop->len >= (int)sizeof(int))
        ret = btif_config_get_int(bdstr, BTIF_STORAGE_PATH_REMOTE_DEVTIME,
                                  (int*)prop->val);
      break;
    case BT_PROPERTY_BDNAME: {
      int len = prop->len;
      if (remote_bd_addr)
        ret = btif_config_get_str(bdstr, BTIF_STORAGE_PATH_REMOTE_NAME,
                                  (char*)prop->val, &len);
      else
        ret = btif_config_get_str("Adapter", BTIF_STORAGE_KEY_ADAPTER_NAME,
                                  (char*)prop->val, &len);
      if (ret && len && len <= prop->len)
        prop->len = len - 1;
      else {
        prop->len = 0;
        ret = false;
      }
      break;
    }
    case BT_PROPERTY_REMOTE_FRIENDLY_NAME: {
      int len = prop->len;
      ret = btif_config_get_str(bdstr, BTIF_STORAGE_PATH_REMOTE_ALIASE,
                                (char*)prop->val, &len);
      if (ret && len && len <= prop->len)
        prop->len = len - 1;
      else {
        prop->len = 0;
        ret = false;
      }
      break;
    }
    case BT_PROPERTY_ADAPTER_SCAN_MODE:
      if (prop->len >= (int)sizeof(int))
        ret = btif_config_get_int("Adapter", BTIF_STORAGE_KEY_ADAPTER_SCANMODE,
                                  (int*)prop->val);
      break;

    case BT_PROPERTY_LOCAL_IO_CAPS:
      if (prop->len >= (int)sizeof(int))
        ret = btif_config_get_int("Adapter", BTIF_STORAGE_KEY_LOCAL_IO_CAPS,
                                  (int*)prop->val);
      break;
    case BT_PROPERTY_LOCAL_IO_CAPS_BLE:
      if (prop->len >= (int)sizeof(int))
        ret = btif_config_get_int("Adapter", BTIF_STORAGE_KEY_LOCAL_IO_CAPS_BLE,
                                  (int*)prop->val);
      break;

    case BT_PROPERTY_ADAPTER_DISCOVERABLE_TIMEOUT:
      if (prop->len >= (int)sizeof(int))
        ret = btif_config_get_int(
            "Adapter", BTIF_STORAGE_KEY_ADAPTER_DISC_TIMEOUT, (int*)prop->val);
      break;
    case BT_PROPERTY_CLASS_OF_DEVICE:
      if (prop->len >= (int)sizeof(int))
        ret = btif_config_get_int(bdstr, BTIF_STORAGE_PATH_REMOTE_DEVCLASS,
                                  (int*)prop->val);
      break;
    case BT_PROPERTY_TYPE_OF_DEVICE:
      if (prop->len >= (int)sizeof(int))
        ret = btif_config_get_int(bdstr, BTIF_STORAGE_PATH_REMOTE_DEVTYPE,
                                  (int*)prop->val);
      break;
    case BT_PROPERTY_UUIDS: {
      char value[1280];
      int size = sizeof(value);
      if (btif_config_get_str(bdstr, BTIF_STORAGE_PATH_REMOTE_SERVICE, value,
                              &size)) {
        Uuid* p_uuid = reinterpret_cast<Uuid*>(prop->val);
        size_t num_uuids =
            btif_split_uuids_string(value, p_uuid, BT_MAX_NUM_UUIDS);
        prop->len = num_uuids * sizeof(Uuid);
        ret = true;
      } else {
        prop->val = NULL;
        prop->len = 0;
      }
    } break;

    case BT_PROPERTY_REMOTE_VERSION_INFO: {
      bt_remote_version_t* info = (bt_remote_version_t*)prop->val;

      if (prop->len >= (int)sizeof(bt_remote_version_t)) {
        ret = btif_config_get_int(bdstr, BT_CONFIG_KEY_REMOTE_VER_MFCT,
                                  &info->manufacturer);

        if (ret)
          ret = btif_config_get_int(bdstr, BT_CONFIG_KEY_REMOTE_VER_VER,
                                    &info->version);

        if (ret)
          ret = btif_config_get_int(bdstr, BT_CONFIG_KEY_REMOTE_VER_SUBVER,
                                    &info->sub_ver);
      }
    } break;

    case BT_PROPERTY_REMOTE_SECURE_CONNECTIONS_SUPPORTED: {
      int val;

      if (prop->len >= (int)sizeof(uint8_t)) {
        ret = btif_config_get_int(
            bdstr, BTIF_STORAGE_KEY_SECURE_CONNECTIONS_SUPPORTED, &val);
        *(uint8_t*)prop->val = (uint8_t)val;
      }
    } break;

    case BT_PROPERTY_REMOTE_MAX_SESSION_KEY_SIZE: {
      int val;

      if (prop->len >= (int)sizeof(uint8_t)) {
        ret = btif_config_get_int(bdstr, BTIF_STORAGE_KEY_MAX_SESSION_KEY_SIZE,
                                  &val);
        *(uint8_t*)prop->val = (uint8_t)val;
      }
    } break;

    default:
      BTIF_TRACE_ERROR("Unknow prop type:%d", prop->type);
      return false;
  }
  return ret;
}

/*******************************************************************************
 *
 * Function         btif_in_fetch_bonded_devices
 *
 * Description      Internal helper function to fetch the bonded devices
 *                  from NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if successful, BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
static bt_status_t btif_in_fetch_bonded_device(const std::string& bdstr) {
  bool bt_linkkey_file_found = false;

  LinkKey link_key;
  size_t size = link_key.size();
  if (btif_config_get_bin(bdstr, "LinkKey", link_key.data(), &size)) {
    int linkkey_type;
    if (btif_config_get_int(bdstr, "LinkKeyType", &linkkey_type)) {
      bt_linkkey_file_found = true;
    } else {
      bt_linkkey_file_found = false;
    }
  }
  if ((btif_in_fetch_bonded_ble_device(bdstr, false, NULL) !=
       BT_STATUS_SUCCESS) &&
      (!bt_linkkey_file_found)) {
    return BT_STATUS_FAIL;
  }
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         btif_in_fetch_bonded_devices
 *
 * Description      Internal helper function to fetch the bonded devices
 *                  from NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if successful, BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
static bt_status_t btif_in_fetch_bonded_devices(
    btif_bonded_devices_t* p_bonded_devices, int add) {
  memset(p_bonded_devices, 0, sizeof(btif_bonded_devices_t));

  bool bt_linkkey_file_found = false;
  int device_type;

  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    auto name = bd_addr.ToString();

    BTIF_TRACE_DEBUG("Remote device:%s", name.c_str());
    LinkKey link_key;
    size_t size = sizeof(link_key);
    if (btif_config_get_bin(name, "LinkKey", link_key.data(), &size)) {
      int linkkey_type;
      if (btif_config_get_int(name, "LinkKeyType", &linkkey_type)) {
        if (add) {
          DEV_CLASS dev_class = {0, 0, 0};
          int cod;
          int pin_length = 0;
          if (btif_config_get_int(name, "DevClass", &cod))
            uint2devclass((uint32_t)cod, dev_class);
          btif_config_get_int(name, "PinLength", &pin_length);
          BTA_DmAddDevice(bd_addr, dev_class, link_key, (uint8_t)linkkey_type,
                          pin_length);

          if (btif_config_get_int(name, "DevType", &device_type) &&
              (device_type == BT_DEVICE_TYPE_DUMO)) {
            btif_gatts_add_bonded_dev_from_nv(bd_addr);
          }
        }
        bt_linkkey_file_found = true;
        if (p_bonded_devices->num_devices < BTM_SEC_MAX_DEVICE_RECORDS) {
          p_bonded_devices->devices[p_bonded_devices->num_devices++] = bd_addr;
        } else {
          BTIF_TRACE_WARNING("%s: exceed the max number of bonded devices",
                             __func__);
        }
      } else {
        bt_linkkey_file_found = false;
      }
    }
    if (!btif_in_fetch_bonded_ble_device(name, add, p_bonded_devices) && !bt_linkkey_file_found) {
      LOG_VERBOSE("No link key or ble key found for device:%s", name.c_str());
    }
  }
  return BT_STATUS_SUCCESS;
}

static void btif_read_le_key(const uint8_t key_type, const size_t key_len,
                             RawAddress bd_addr, const tBLE_ADDR_TYPE addr_type,
                             const bool add_key, bool* device_added,
                             bool* key_found) {
  CHECK(device_added);
  CHECK(key_found);

  tBTA_LE_KEY_VALUE key;
  memset(&key, 0, sizeof(key));

  if (btif_storage_get_ble_bonding_key(bd_addr, key_type, (uint8_t*)&key,
                                       key_len) == BT_STATUS_SUCCESS) {
    if (add_key) {
      if (!*device_added) {
        BTA_DmAddBleDevice(bd_addr, addr_type, BT_DEVICE_TYPE_BLE);
        *device_added = true;
      }

      BTIF_TRACE_DEBUG("%s() Adding key type %d for %s", __func__, key_type,
                       bd_addr.ToString().c_str());
      BTA_DmAddBleKey(bd_addr, &key, key_type);
    }

    *key_found = true;
  }
}

/*******************************************************************************
 * Functions
 *
 * Functions are synchronous and can be called by both from internal modules
 * such as BTIF_DM and by external entiries from HAL via BTIF_context_switch.
 * For OUT parameters, the caller is expected to provide the memory.
 * Caller is expected to provide a valid pointer to 'property->value' based on
 * the property->type.
 ******************************************************************************/

/*******************************************************************************
 *
 * Function         btif_split_uuids_string
 *
 * Description      Internal helper function to split the string of UUIDs
 *                  read from the NVRAM to an array
 *
 * Returns          Number of UUIDs parsed from the supplied string
 *
 ******************************************************************************/
size_t btif_split_uuids_string(const char* str, bluetooth::Uuid* p_uuid,
                               size_t max_uuids) {
  CHECK(str);
  CHECK(p_uuid);

  size_t num_uuids = 0;
  while (str && num_uuids < max_uuids) {
    bool is_valid;
    bluetooth::Uuid tmp =
        Uuid::FromString(std::string(str, Uuid::kString128BitLen), &is_valid);
    if (!is_valid) break;

    *p_uuid = tmp;
    p_uuid++;

    num_uuids++;
    str = strchr(str, ' ');
    if (str) str++;
  }

  return num_uuids;
}

/**
 * Helper function for fetching a local Input/Output capability property. If not
 * set, it returns the default value.
 */
static uint8_t btif_storage_get_io_cap_property(bt_property_type_t type,
                                                uint8_t default_value) {
  char buf[sizeof(int)];

  bt_property_t property;
  property.type = type;
  property.val = (void*)buf;
  property.len = sizeof(int);

  bt_status_t ret = btif_storage_get_adapter_property(&property);

  return (ret == BT_STATUS_SUCCESS) ? (uint8_t)(*(int*)property.val)
                                    : default_value;
}

/*******************************************************************************
 *
 * Function         btif_storage_get_io_caps
 *
 * Description      BTIF storage API - Fetches the local Input/Output
 *                  capabilities of the device.
 *
 * Returns          Returns local IO Capability of device. If not stored,
 *                  returns BTM_LOCAL_IO_CAPS.
 *
 ******************************************************************************/
uint8_t btif_storage_get_local_io_caps() {
  return btif_storage_get_io_cap_property(BT_PROPERTY_LOCAL_IO_CAPS,
                                          BTM_LOCAL_IO_CAPS);
}

/*******************************************************************************
 *
 * Function         btif_storage_get_io_caps_ble
 *
 * Description      BTIF storage API - Fetches the local Input/Output
 *                  capabilities of the BLE device.
 *
 * Returns          Returns local IO Capability of BLE device. If not stored,
 *                  returns BTM_LOCAL_IO_CAPS_BLE.
 *
 ******************************************************************************/
uint8_t btif_storage_get_local_io_caps_ble() {
  return btif_storage_get_io_cap_property(BT_PROPERTY_LOCAL_IO_CAPS_BLE,
                                          BTM_LOCAL_IO_CAPS_BLE);
}

/*******************************************************************************
 *
 * Function         btif_storage_get_adapter_property
 *
 * Description      BTIF storage API - Fetches the adapter property->type
 *                  from NVRAM and fills property->val.
 *                  Caller should provide memory for property->val and
 *                  set the property->val
 *
 * Returns          BT_STATUS_SUCCESS if the fetch was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_get_adapter_property(bt_property_t* property) {
  /* Special handling for adapter address and BONDED_DEVICES */
  if (property->type == BT_PROPERTY_BDADDR) {
    RawAddress* bd_addr = (RawAddress*)property->val;
    /* Fetch the local BD ADDR */
    const controller_t* controller = controller_get_interface();
    if (!controller->get_is_ready()) {
      LOG_ERROR("%s: Controller not ready! Unable to return Bluetooth Address",
                __func__);
      *bd_addr = RawAddress::kEmpty;
      return BT_STATUS_FAIL;
    } else {
      LOG_ERROR("%s: Controller ready!", __func__);
      *bd_addr = *controller->get_address();
    }
    property->len = RawAddress::kLength;
    return BT_STATUS_SUCCESS;
  } else if (property->type == BT_PROPERTY_ADAPTER_BONDED_DEVICES) {
    btif_bonded_devices_t bonded_devices;

    btif_in_fetch_bonded_devices(&bonded_devices, 0);

    BTIF_TRACE_DEBUG(
        "%s: Number of bonded devices: %d "
        "Property:BT_PROPERTY_ADAPTER_BONDED_DEVICES",
        __func__, bonded_devices.num_devices);

    property->len = bonded_devices.num_devices * RawAddress::kLength;
    memcpy(property->val, bonded_devices.devices, property->len);

    /* if there are no bonded_devices, then length shall be 0 */
    return BT_STATUS_SUCCESS;
  } else if (property->type == BT_PROPERTY_UUIDS) {
    /* publish list of local supported services */
    Uuid* p_uuid = reinterpret_cast<Uuid*>(property->val);
    uint32_t num_uuids = 0;
    uint32_t i;

    tBTA_SERVICE_MASK service_mask = btif_get_enabled_services_mask();
    LOG_INFO("%s service_mask:0x%x", __func__, service_mask);
    for (i = 0; i < BTA_MAX_SERVICE_ID; i++) {
      /* This should eventually become a function when more services are enabled
       */
      if (service_mask & (tBTA_SERVICE_MASK)(1 << i)) {
        switch (i) {
          case BTA_HFP_SERVICE_ID: {
            *(p_uuid + num_uuids) =
                Uuid::From16Bit(UUID_SERVCLASS_AG_HANDSFREE);
            num_uuids++;
          }
            FALLTHROUGH_INTENDED; /* FALLTHROUGH */
          /* intentional fall through: Send both BFP & HSP UUIDs if HFP is
           * enabled */
          case BTA_HSP_SERVICE_ID: {
            *(p_uuid + num_uuids) =
                Uuid::From16Bit(UUID_SERVCLASS_HEADSET_AUDIO_GATEWAY);
            num_uuids++;
          } break;
          case BTA_A2DP_SOURCE_SERVICE_ID: {
            *(p_uuid + num_uuids) =
                Uuid::From16Bit(UUID_SERVCLASS_AUDIO_SOURCE);
            num_uuids++;
          } break;
          case BTA_A2DP_SINK_SERVICE_ID: {
            *(p_uuid + num_uuids) = Uuid::From16Bit(UUID_SERVCLASS_AUDIO_SINK);
            num_uuids++;
          } break;
          case BTA_PBAP_SERVICE_ID: {
            *(p_uuid + num_uuids) = Uuid::From16Bit(UUID_SERVCLASS_PBAP_PSE);
            num_uuids++;
          } break;
          case BTA_HFP_HS_SERVICE_ID: {
            *(p_uuid + num_uuids) =
                Uuid::From16Bit(UUID_SERVCLASS_HF_HANDSFREE);
            num_uuids++;
          } break;
          case BTA_MAP_SERVICE_ID: {
            *(p_uuid + num_uuids) = Uuid::From16Bit(UUID_SERVCLASS_MESSAGE_ACCESS);
            num_uuids++;
          } break;
          case BTA_MN_SERVICE_ID: {
            *(p_uuid + num_uuids) = Uuid::From16Bit(UUID_SERVCLASS_MESSAGE_NOTIFICATION);
            num_uuids++;
          } break;
          case BTA_PCE_SERVICE_ID: {
            *(p_uuid + num_uuids) = Uuid::From16Bit(UUID_SERVCLASS_PBAP_PCE);
            num_uuids++;
          } break;
        }
      }
    }
    property->len = (num_uuids) * sizeof(Uuid);
    return BT_STATUS_SUCCESS;
  }

  /* fall through for other properties */
  if (!cfg2prop(NULL, property)) {
    return btif_dm_get_adapter_property(property);
  }
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         btif_storage_set_adapter_property
 *
 * Description      BTIF storage API - Stores the adapter property
 *                  to NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if the store was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_set_adapter_property(bt_property_t* property) {
  return prop2cfg(NULL, property) ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

/*******************************************************************************
 *
 * Function         btif_storage_get_remote_device_property
 *
 * Description      BTIF storage API - Fetches the remote device property->type
 *                  from NVRAM and fills property->val.
 *                  Caller should provide memory for property->val and
 *                  set the property->val
 *
 * Returns          BT_STATUS_SUCCESS if the fetch was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_get_remote_device_property(
    const RawAddress* remote_bd_addr, bt_property_t* property) {
  return cfg2prop(remote_bd_addr, property) ? BT_STATUS_SUCCESS
                                            : BT_STATUS_FAIL;
}
/*******************************************************************************
 *
 * Function         btif_storage_set_remote_device_property
 *
 * Description      BTIF storage API - Stores the remote device property
 *                  to NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if the store was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_set_remote_device_property(
    const RawAddress* remote_bd_addr, bt_property_t* property) {
  return prop2cfg(remote_bd_addr, property) ? BT_STATUS_SUCCESS
                                            : BT_STATUS_FAIL;
}

/*******************************************************************************
 *
 * Function         btif_storage_add_remote_device
 *
 * Description      BTIF storage API - Adds a newly discovered device to NVRAM
 *                  along with the timestamp. Also, stores the various
 *                  properties - RSSI, BDADDR, NAME (if found in EIR)
 *
 * Returns          BT_STATUS_SUCCESS if the store was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_add_remote_device(const RawAddress* remote_bd_addr,
                                           uint32_t num_properties,
                                           bt_property_t* properties) {
  uint32_t i = 0;
  /* TODO: If writing a property, fails do we go back undo the earlier
   * written properties? */
  for (i = 0; i < num_properties; i++) {
    /* Ignore the RSSI as this is not stored in DB */
    if (properties[i].type == BT_PROPERTY_REMOTE_RSSI) continue;

    /* address for remote device needs special handling as we also store
     * timestamp */
    if (properties[i].type == BT_PROPERTY_BDADDR) {
      bt_property_t addr_prop;
      memcpy(&addr_prop, &properties[i], sizeof(bt_property_t));
      addr_prop.type = (bt_property_type_t)BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP;
      btif_storage_set_remote_device_property(remote_bd_addr, &addr_prop);
    } else {
      btif_storage_set_remote_device_property(remote_bd_addr, &properties[i]);
    }
  }
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         btif_storage_add_bonded_device
 *
 * Description      BTIF storage API - Adds the newly bonded device to NVRAM
 *                  along with the link-key, Key type and Pin key length
 *
 * Returns          BT_STATUS_SUCCESS if the store was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/

bt_status_t btif_storage_add_bonded_device(RawAddress* remote_bd_addr,
                                           LinkKey link_key, uint8_t key_type,
                                           uint8_t pin_length) {
  std::string bdstr = remote_bd_addr->ToString();
  int ret = btif_config_set_int(bdstr, "LinkKeyType", (int)key_type);
  ret &= btif_config_set_int(bdstr, "PinLength", (int)pin_length);
  ret &=
      btif_config_set_bin(bdstr, "LinkKey", link_key.data(), link_key.size());

  if (is_restricted_mode()) {
    BTIF_TRACE_WARNING("%s: '%s' pairing will be removed if unrestricted",
                       __func__, bdstr.c_str());
    btif_config_set_int(bdstr, "Restricted", 1);
  }

  /* write bonded info immediately */
  btif_config_flush();
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

/*******************************************************************************
 *
 * Function         btif_storage_remove_bonded_device
 *
 * Description      BTIF storage API - Deletes the bonded device from NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if the deletion was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_remove_bonded_device(
    const RawAddress* remote_bd_addr) {
  std::string bdstr = remote_bd_addr->ToString();
  LOG_INFO("Removing bonded device addr:%s", bdstr.c_str());

  btif_storage_remove_ble_bonding_keys(remote_bd_addr);

  int ret = 1;
  if (btif_config_exist(bdstr, "LinkKeyType"))
    ret &= btif_config_remove(bdstr, "LinkKeyType");
  if (btif_config_exist(bdstr, "PinLength"))
    ret &= btif_config_remove(bdstr, "PinLength");
  if (btif_config_exist(bdstr, "LinkKey"))
    ret &= btif_config_remove(bdstr, "LinkKey");
  if (btif_config_exist(bdstr, BTIF_STORAGE_PATH_REMOTE_ALIASE)) {
    ret &= btif_config_remove(bdstr, BTIF_STORAGE_PATH_REMOTE_ALIASE);
  }
  if (btif_config_exist(bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_SUPPORTED)) {
    ret &= btif_config_remove(bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_SUPPORTED);
  }
  if (btif_config_exist(bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_DB_HASH)) {
    ret &= btif_config_remove(bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_DB_HASH);
  }
  if (btif_config_exist(bdstr, BTIF_STORAGE_KEY_GATT_SERVER_SUPPORTED)) {
    ret &= btif_config_remove(bdstr, BTIF_STORAGE_KEY_GATT_SERVER_SUPPORTED);
  }

  /* write bonded info immediately */
  btif_config_flush();

  /* Check the length of the paired devices, and if 0 then reset IRK */
  auto paired_devices = btif_config_get_paired_devices();
  if (paired_devices.empty() &&
      bluetooth::common::init_flags::irk_rotation_is_enabled()) {
    LOG_INFO("Last paired device removed, resetting IRK");
    BTA_DmBleResetId();
  }
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

/* Some devices hardcode sample LTK value from spec, instead of generating one.
 * Treat such devices as insecure, and remove such bonds when bluetooth
 * restarts. Removing them after disconnection is handled separately.
 *
 * We still allow such devices to bond in order to give the user a chance to
 * update firmware.
 */
static void remove_devices_with_sample_ltk() {
  std::vector<RawAddress> bad_ltk;
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    auto name = bd_addr.ToString();

    tBTA_LE_KEY_VALUE key;
    memset(&key, 0, sizeof(key));

    if (btif_storage_get_ble_bonding_key(
            bd_addr, BTM_LE_KEY_PENC, (uint8_t*)&key,
            sizeof(tBTM_LE_PENC_KEYS)) == BT_STATUS_SUCCESS) {
      if (is_sample_ltk(key.penc_key.ltk)) {
        bad_ltk.push_back(bd_addr);
      }
    }
  }

  for (RawAddress address : bad_ltk) {
    LOG(ERROR) << __func__
               << ": removing bond to device using test TLK: " << address;

    btif_storage_remove_bonded_device(&address);
  }
}

/*******************************************************************************
 *
 * Function         btif_storage_load_le_devices
 *
 * Description      BTIF storage API - Loads all LE-only and Dual Mode devices
 *                  from NVRAM. This API invokes the adaper_properties_cb.
 *                  It also invokes invoke_address_consolidate_cb
 *                  to consolidate each Dual Mode device and
 *                  invoke_le_address_associate_cb to associate each LE-only
 *                  device between its RPA and identity address.
 *
 ******************************************************************************/
void btif_storage_load_le_devices(void) {
  btif_bonded_devices_t bonded_devices;
  btif_in_fetch_bonded_devices(&bonded_devices, 1);
  std::unordered_set<RawAddress> bonded_addresses;
  for (uint16_t i = 0; i < bonded_devices.num_devices; i++) {
    bonded_addresses.insert(bonded_devices.devices[i]);
  }

  std::vector<std::pair<RawAddress, RawAddress>> consolidated_devices;
  for (uint16_t i = 0; i < bonded_devices.num_devices; i++) {
    // RawAddress* p_remote_addr;
    tBTA_LE_KEY_VALUE key = {};
    if (btif_storage_get_ble_bonding_key(
            bonded_devices.devices[i], BTM_LE_KEY_PID, (uint8_t*)&key,
            sizeof(tBTM_LE_PID_KEYS)) == BT_STATUS_SUCCESS) {
      if (bonded_devices.devices[i] != key.pid_key.identity_addr) {
        LOG_INFO("found device with a known identity address %s %s",
                 bonded_devices.devices[i].ToString().c_str(),
                 key.pid_key.identity_addr.ToString().c_str());

        if (bonded_devices.devices[i].IsEmpty() ||
            key.pid_key.identity_addr.IsEmpty()) {
          LOG_WARN("Address is empty! Skip");
        } else {
          consolidated_devices.emplace_back(bonded_devices.devices[i],
                                            key.pid_key.identity_addr);
        }
      }
    }
  }

  bt_property_t adapter_prop = {};
  /* Send the adapter_properties_cb with bonded consolidated device */
  {
    /* BONDED_DEVICES */
    auto devices_list =
        std::make_unique<RawAddress[]>(consolidated_devices.size());
    adapter_prop.type = BT_PROPERTY_ADAPTER_BONDED_DEVICES;
    adapter_prop.len = consolidated_devices.size() * sizeof(RawAddress);
    adapter_prop.val = devices_list.get();
    for (uint16_t i = 0; i < consolidated_devices.size(); i++) {
      devices_list[i] = consolidated_devices[i].first;
    }
    btif_adapter_properties_evt(BT_STATUS_SUCCESS, /* num_props */ 1,
                                &adapter_prop);
  }

  for (const auto& device : consolidated_devices) {
    if (bonded_addresses.find(device.second) != bonded_addresses.end()) {
      // Invokes address consolidation for DuMo devices
      invoke_address_consolidate_cb(device.first, device.second);
    } else {
      // Associates RPA & identity address for LE-only devices
      invoke_le_address_associate_cb(device.first, device.second);
    }
  }
}

/*******************************************************************************
 *
 * Function         btif_storage_load_bonded_devices
 *
 * Description      BTIF storage API - Loads all the bonded devices from NVRAM
 *                  and adds to the BTA.
 *                  Additionally, this API also invokes the adaper_properties_cb
 *                  and remote_device_properties_cb for each of the bonded
 *                  devices.
 *
 * Returns          BT_STATUS_SUCCESS if successful, BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_load_bonded_devices(void) {
  btif_bonded_devices_t bonded_devices;
  uint32_t i = 0;
  bt_property_t adapter_props[6];
  uint32_t num_props = 0;
  bt_property_t remote_properties[8];
  RawAddress addr;
  bt_bdname_t name, alias;
  bt_scan_mode_t mode;
  uint32_t disc_timeout;
  Uuid local_uuids[BT_MAX_NUM_UUIDS];
  Uuid remote_uuids[BT_MAX_NUM_UUIDS];
  bt_status_t status;

  remove_devices_with_sample_ltk();

  btif_in_fetch_bonded_devices(&bonded_devices, 1);

  /* Now send the adapter_properties_cb with all adapter_properties */
  {
    memset(adapter_props, 0, sizeof(adapter_props));

    /* address */
    BTIF_STORAGE_GET_ADAPTER_PROP(status, BT_PROPERTY_BDADDR, &addr,
                                  sizeof(addr), adapter_props[num_props]);
    // Add BT_PROPERTY_BDADDR property into list only when successful.
    // Otherwise, skip this property entry.
    if (status == BT_STATUS_SUCCESS) {
      num_props++;
    }

    /* BD_NAME */
    BTIF_STORAGE_GET_ADAPTER_PROP(status, BT_PROPERTY_BDNAME, &name,
                                  sizeof(name), adapter_props[num_props]);
    num_props++;

    /* SCAN_MODE */
    /* TODO: At the time of BT on, always report the scan mode as 0 irrespective
     of the scan_mode during the previous enable cycle.
     This needs to be re-visited as part of the app/stack enable sequence
     synchronization */
    mode = BT_SCAN_MODE_NONE;
    adapter_props[num_props].type = BT_PROPERTY_ADAPTER_SCAN_MODE;
    adapter_props[num_props].len = sizeof(mode);
    adapter_props[num_props].val = &mode;
    num_props++;

    /* DISC_TIMEOUT */
    BTIF_STORAGE_GET_ADAPTER_PROP(
        status, BT_PROPERTY_ADAPTER_DISCOVERABLE_TIMEOUT, &disc_timeout,
        sizeof(disc_timeout), adapter_props[num_props]);
    num_props++;

    /* BONDED_DEVICES */
    RawAddress* devices_list = (RawAddress*)osi_malloc(
        sizeof(RawAddress) * bonded_devices.num_devices);
    adapter_props[num_props].type = BT_PROPERTY_ADAPTER_BONDED_DEVICES;
    adapter_props[num_props].len =
        bonded_devices.num_devices * sizeof(RawAddress);
    adapter_props[num_props].val = devices_list;
    for (i = 0; i < bonded_devices.num_devices; i++) {
      devices_list[i] = bonded_devices.devices[i];
    }
    num_props++;

    /* LOCAL UUIDs */
    BTIF_STORAGE_GET_ADAPTER_PROP(status, BT_PROPERTY_UUIDS, local_uuids,
                                  sizeof(local_uuids),
                                  adapter_props[num_props]);
    num_props++;

    btif_adapter_properties_evt(BT_STATUS_SUCCESS, num_props, adapter_props);

    osi_free(devices_list);
  }

  BTIF_TRACE_EVENT("%s: %d bonded devices found", __func__,
                   bonded_devices.num_devices);

  {
    for (i = 0; i < bonded_devices.num_devices; i++) {
      RawAddress* p_remote_addr;

      /*
       * TODO: improve handling of missing fields in NVRAM.
       */
      uint32_t cod = 0;
      uint32_t devtype = 0;

      num_props = 0;
      p_remote_addr = &bonded_devices.devices[i];
      memset(remote_properties, 0, sizeof(remote_properties));
      BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr, BT_PROPERTY_BDNAME, &name,
                                   sizeof(name), remote_properties[num_props]);
      num_props++;

      BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr,
                                   BT_PROPERTY_REMOTE_FRIENDLY_NAME, &alias,
                                   sizeof(alias), remote_properties[num_props]);
      num_props++;

      BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr, BT_PROPERTY_CLASS_OF_DEVICE,
                                   &cod, sizeof(cod),
                                   remote_properties[num_props]);
      num_props++;

      BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr, BT_PROPERTY_TYPE_OF_DEVICE,
                                   &devtype, sizeof(devtype),
                                   remote_properties[num_props]);
      num_props++;

      BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr, BT_PROPERTY_UUIDS,
                                   remote_uuids, sizeof(remote_uuids),
                                   remote_properties[num_props]);
      num_props++;

      btif_remote_properties_evt(BT_STATUS_SUCCESS, p_remote_addr, num_props,
                                 remote_properties);
    }
  }
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         btif_storage_add_ble_bonding_key
 *
 * Description      BTIF storage API - Adds the newly bonded device to NVRAM
 *                  along with the ble-key, Key type and Pin key length
 *
 * Returns          BT_STATUS_SUCCESS if the store was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/

bt_status_t btif_storage_add_ble_bonding_key(RawAddress* remote_bd_addr,
                                             const uint8_t* key,
                                             uint8_t key_type,
                                             uint8_t key_length) {
  const char* name;
  switch (key_type) {
    case BTM_LE_KEY_PENC:
      name = "LE_KEY_PENC";
      break;
    case BTM_LE_KEY_PID:
      name = "LE_KEY_PID";
      break;
    case BTM_LE_KEY_PCSRK:
      name = "LE_KEY_PCSRK";
      break;
    case BTM_LE_KEY_LENC:
      name = "LE_KEY_LENC";
      break;
    case BTM_LE_KEY_LCSRK:
      name = "LE_KEY_LCSRK";
      break;
    case BTM_LE_KEY_LID:
      name = "LE_KEY_LID";
      break;
    default:
      return BT_STATUS_FAIL;
  }
  int ret =
      btif_config_set_bin(remote_bd_addr->ToString(), name, key, key_length);
  btif_config_save();
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

/*******************************************************************************
 *
 * Function         btif_storage_get_ble_bonding_key
 *
 * Description
 *
 * Returns          BT_STATUS_SUCCESS if the fetch was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_get_ble_bonding_key(const RawAddress& remote_bd_addr,
                                             uint8_t key_type,
                                             uint8_t* key_value,
                                             int key_length) {
  const char* name;
  switch (key_type) {
    case BTM_LE_KEY_PENC:
      name = "LE_KEY_PENC";
      break;
    case BTM_LE_KEY_PID:
      name = "LE_KEY_PID";
      break;
    case BTM_LE_KEY_PCSRK:
      name = "LE_KEY_PCSRK";
      break;
    case BTM_LE_KEY_LENC:
      name = "LE_KEY_LENC";
      break;
    case BTM_LE_KEY_LCSRK:
      name = "LE_KEY_LCSRK";
      break;
    case BTM_LE_KEY_LID:
      name = "LE_KEY_LID";
      break;
    default:
      return BT_STATUS_FAIL;
  }
  size_t length = key_length;
  int ret =
      btif_config_get_bin(remote_bd_addr.ToString(), name, key_value, &length);
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

/*******************************************************************************
 *
 * Function         btif_storage_remove_ble_keys
 *
 * Description      BTIF storage API - Deletes the bonded device from NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if the deletion was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_remove_ble_bonding_keys(
    const RawAddress* remote_bd_addr) {
  std::string bdstr = remote_bd_addr->ToString();
  LOG_INFO("Removing bonding keys for bd addr:%s", bdstr.c_str());
  int ret = 1;
  if (btif_config_exist(bdstr, "LE_KEY_PENC"))
    ret &= btif_config_remove(bdstr, "LE_KEY_PENC");
  if (btif_config_exist(bdstr, "LE_KEY_PID"))
    ret &= btif_config_remove(bdstr, "LE_KEY_PID");
  if (btif_config_exist(bdstr, "LE_KEY_PCSRK"))
    ret &= btif_config_remove(bdstr, "LE_KEY_PCSRK");
  if (btif_config_exist(bdstr, "LE_KEY_LENC"))
    ret &= btif_config_remove(bdstr, "LE_KEY_LENC");
  if (btif_config_exist(bdstr, "LE_KEY_LCSRK"))
    ret &= btif_config_remove(bdstr, "LE_KEY_LCSRK");
  btif_config_save();
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

/*******************************************************************************
 *
 * Function         btif_storage_add_ble_local_key
 *
 * Description      BTIF storage API - Adds the ble key to NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if the store was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_add_ble_local_key(const Octet16& key,
                                           uint8_t key_type) {
  const char* name;
  switch (key_type) {
    case BTIF_DM_LE_LOCAL_KEY_IR:
      name = "LE_LOCAL_KEY_IR";
      break;
    case BTIF_DM_LE_LOCAL_KEY_IRK:
      name = "LE_LOCAL_KEY_IRK";
      break;
    case BTIF_DM_LE_LOCAL_KEY_DHK:
      name = "LE_LOCAL_KEY_DHK";
      break;
    case BTIF_DM_LE_LOCAL_KEY_ER:
      name = "LE_LOCAL_KEY_ER";
      break;
    default:
      return BT_STATUS_FAIL;
  }
  int ret = btif_config_set_bin("Adapter", name, key.data(), key.size());
  // Had to change this to flush to get it to work on test.
  // Seems to work in the real world on a phone... but not sure why there's a
  // race in test. Investigate b/239828132
  btif_config_flush();
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

/** Stores local key of |key_type| into |key_value|
 * Returns BT_STATUS_SUCCESS if the fetch was successful, BT_STATUS_FAIL
 * otherwise
 */
bt_status_t btif_storage_get_ble_local_key(uint8_t key_type,
                                           Octet16* key_value) {
  const char* name;
  switch (key_type) {
    case BTIF_DM_LE_LOCAL_KEY_IR:
      name = "LE_LOCAL_KEY_IR";
      break;
    case BTIF_DM_LE_LOCAL_KEY_IRK:
      name = "LE_LOCAL_KEY_IRK";
      break;
    case BTIF_DM_LE_LOCAL_KEY_DHK:
      name = "LE_LOCAL_KEY_DHK";
      break;
    case BTIF_DM_LE_LOCAL_KEY_ER:
      name = "LE_LOCAL_KEY_ER";
      break;
    default:
      return BT_STATUS_FAIL;
  }
  size_t length = key_value->size();
  int ret = btif_config_get_bin("Adapter", name, key_value->data(), &length);
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

/*******************************************************************************
 *
 * Function         btif_storage_remove_ble_local_keys
 *
 * Description      BTIF storage API - Deletes the bonded device from NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if the deletion was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_remove_ble_local_keys(void) {
  int ret = 1;
  if (btif_config_exist("Adapter", "LE_LOCAL_KEY_IR"))
    ret &= btif_config_remove("Adapter", "LE_LOCAL_KEY_IR");
  if (btif_config_exist("Adapter", "LE_LOCAL_KEY_IRK"))
    ret &= btif_config_remove("Adapter", "LE_LOCAL_KEY_IRK");
  if (btif_config_exist("Adapter", "LE_LOCAL_KEY_DHK"))
    ret &= btif_config_remove("Adapter", "LE_LOCAL_KEY_DHK");
  if (btif_config_exist("Adapter", "LE_LOCAL_KEY_ER"))
    ret &= btif_config_remove("Adapter", "LE_LOCAL_KEY_ER");
  btif_config_save();
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

static bt_status_t btif_in_fetch_bonded_ble_device(
    const std::string& remote_bd_addr, int add,
    btif_bonded_devices_t* p_bonded_devices) {
  int device_type;
  tBLE_ADDR_TYPE addr_type;
  bool device_added = false;
  bool key_found = false;

  if (!btif_config_get_int(remote_bd_addr, "DevType", &device_type))
    return BT_STATUS_FAIL;

  if ((device_type & BT_DEVICE_TYPE_BLE) == BT_DEVICE_TYPE_BLE ||
      btif_has_ble_keys(remote_bd_addr)) {
    BTIF_TRACE_DEBUG("%s Found a LE device: %s", __func__,
                     remote_bd_addr.c_str());

    RawAddress bd_addr;
    RawAddress::FromString(remote_bd_addr, bd_addr);

    if (btif_storage_get_remote_addr_type(&bd_addr, &addr_type) !=
        BT_STATUS_SUCCESS) {
      addr_type = BLE_ADDR_PUBLIC;
      btif_storage_set_remote_addr_type(&bd_addr, BLE_ADDR_PUBLIC);
    }

    btif_read_le_key(BTM_LE_KEY_PENC, sizeof(tBTM_LE_PENC_KEYS), bd_addr,
                     addr_type, add, &device_added, &key_found);

    btif_read_le_key(BTM_LE_KEY_PID, sizeof(tBTM_LE_PID_KEYS), bd_addr,
                     addr_type, add, &device_added, &key_found);

    btif_read_le_key(BTM_LE_KEY_LID, sizeof(tBTM_LE_PID_KEYS), bd_addr,
                     addr_type, add, &device_added, &key_found);

    btif_read_le_key(BTM_LE_KEY_PCSRK, sizeof(tBTM_LE_PCSRK_KEYS), bd_addr,
                     addr_type, add, &device_added, &key_found);

    btif_read_le_key(BTM_LE_KEY_LENC, sizeof(tBTM_LE_LENC_KEYS), bd_addr,
                     addr_type, add, &device_added, &key_found);

    btif_read_le_key(BTM_LE_KEY_LCSRK, sizeof(tBTM_LE_LCSRK_KEYS), bd_addr,
                     addr_type, add, &device_added, &key_found);

    // Fill in the bonded devices
    if (device_added) {
      if (p_bonded_devices->num_devices < BTM_SEC_MAX_DEVICE_RECORDS) {
        p_bonded_devices->devices[p_bonded_devices->num_devices++] = bd_addr;
      } else {
        BTIF_TRACE_WARNING("%s: exceed the max number of bonded devices",
                           __func__);
      }
      btif_gatts_add_bonded_dev_from_nv(bd_addr);
    }

    if (key_found) return BT_STATUS_SUCCESS;
  }
  return BT_STATUS_FAIL;
}

bt_status_t btif_storage_set_remote_addr_type(const RawAddress* remote_bd_addr,
                                              tBLE_ADDR_TYPE addr_type) {
  int ret = btif_config_set_int(remote_bd_addr->ToString(), "AddrType",
                                (int)addr_type);
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

void btif_storage_set_remote_addr_type(const RawAddress& remote_bd_addr,
                                       const tBLE_ADDR_TYPE& addr_type) {
  if (!btif_config_set_int(remote_bd_addr.ToString(), "AddrType",
                           static_cast<int>(addr_type)))
    LOG_ERROR("Unable to set storage property");
}

void btif_storage_set_remote_device_type(const RawAddress& remote_bd_addr,
                                         const tBT_DEVICE_TYPE& device_type) {
  if (!btif_config_set_int(remote_bd_addr.ToString(), "DevType",
                           static_cast<int>(device_type)))
    LOG_ERROR("Unable to set storage property");
}

bool btif_has_ble_keys(const std::string& bdstr) {
  return btif_config_exist(bdstr, "LE_KEY_PENC");
}

/*******************************************************************************
 *
 * Function         btif_storage_get_remote_addr_type
 *
 * Description      BTIF storage API - Fetches the remote addr type
 *
 * Returns          BT_STATUS_SUCCESS if the fetch was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_get_remote_addr_type(const RawAddress* remote_bd_addr,
                                              tBLE_ADDR_TYPE* addr_type) {
  int val;
  int ret = btif_config_get_int(remote_bd_addr->ToString(), "AddrType", &val);
  *addr_type = static_cast<tBLE_ADDR_TYPE>(val);
  return ret ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

bool btif_storage_get_remote_addr_type(const RawAddress& remote_bd_addr,
                                       tBLE_ADDR_TYPE& addr_type) {
  int val;
  const int ret =
      btif_config_get_int(remote_bd_addr.ToString(), "AddrType", &val);
  addr_type = static_cast<tBLE_ADDR_TYPE>(val);
  return ret;
}

bool btif_storage_get_remote_device_type(const RawAddress& remote_bd_addr,
                                         tBT_DEVICE_TYPE& device_type) {
  int val;
  const bool ret =
      btif_config_get_int(remote_bd_addr.ToString(), "DevType", &val);
  device_type = static_cast<tBT_DEVICE_TYPE>(val);
  return ret;
}

/*******************************************************************************
 *
 * Function         btif_storage_set_hid_connection_policy
 *
 * Description      Stores connection policy info in nvram
 *
 * Returns          BT_STATUS_SUCCESS
 *
 ******************************************************************************/
bt_status_t btif_storage_set_hid_connection_policy(const RawAddress& addr,
                                                   bool reconnect_allowed) {
  std::string bdstr = addr.ToString();

  if (btif_config_set_int(bdstr, BTIF_STORAGE_KEY_HID_RECONNECT_ALLOWED,
                          reconnect_allowed)) {
    return BT_STATUS_SUCCESS;
  } else {
    return BT_STATUS_FAIL;
  }
}

/*******************************************************************************
 *
 * Function         btif_storage_get_hid_connection_policy
 *
 * Description      get connection policy info from nvram
 *
 * Returns          BT_STATUS_SUCCESS
 *
 ******************************************************************************/
bt_status_t btif_storage_get_hid_connection_policy(const RawAddress& addr,
                                                   bool* reconnect_allowed) {
  std::string bdstr = addr.ToString();

  // For backward compatibility, assume that the reconnection is allowed in the
  // absence of the key
  int value = 1;
  btif_config_get_int(bdstr, BTIF_STORAGE_KEY_HID_RECONNECT_ALLOWED, &value);
  *reconnect_allowed = (value != 0);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         btif_storage_add_hid_device_info
 *
 * Description      BTIF storage API - Adds the hid information of bonded hid
 *                  devices-to NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if the store was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/

bt_status_t btif_storage_add_hid_device_info(
    RawAddress* remote_bd_addr, uint16_t attr_mask, uint8_t sub_class,
    uint8_t app_id, uint16_t vendor_id, uint16_t product_id, uint16_t version,
    uint8_t ctry_code, uint16_t ssr_max_latency, uint16_t ssr_min_tout,
    uint16_t dl_len, uint8_t* dsc_list) {
  BTIF_TRACE_DEBUG("btif_storage_add_hid_device_info:");
  std::string bdstr = remote_bd_addr->ToString();
  btif_config_set_int(bdstr, "HidAttrMask", attr_mask);
  btif_config_set_int(bdstr, "HidSubClass", sub_class);
  btif_config_set_int(bdstr, "HidAppId", app_id);
  btif_config_set_int(bdstr, "HidVendorId", vendor_id);
  btif_config_set_int(bdstr, "HidProductId", product_id);
  btif_config_set_int(bdstr, "HidVersion", version);
  btif_config_set_int(bdstr, "HidCountryCode", ctry_code);
  btif_config_set_int(bdstr, "HidSSRMaxLatency", ssr_max_latency);
  btif_config_set_int(bdstr, "HidSSRMinTimeout", ssr_min_tout);
  if (dl_len > 0) btif_config_set_bin(bdstr, "HidDescriptor", dsc_list, dl_len);
  btif_config_save();
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         btif_storage_load_bonded_hid_info
 *
 * Description      BTIF storage API - Loads hid info for all the bonded devices
 *                  from NVRAM and adds those devices  to the BTA_HH.
 *
 * Returns          BT_STATUS_SUCCESS if successful, BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_load_bonded_hid_info(void) {
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    auto name = bd_addr.ToString();

    BTIF_TRACE_DEBUG("Remote device:%s", name.c_str());

    int value;
    if (!btif_config_get_int(name, "HidAttrMask", &value)) continue;
    uint16_t attr_mask = (uint16_t)value;

    if (btif_in_fetch_bonded_device(name) != BT_STATUS_SUCCESS) {
      btif_storage_remove_hid_info(bd_addr);
      continue;
    }

    tBTA_HH_DEV_DSCP_INFO dscp_info;
    memset(&dscp_info, 0, sizeof(dscp_info));

    btif_config_get_int(name, "HidSubClass", &value);
    uint8_t sub_class = (uint8_t)value;

    btif_config_get_int(name, "HidAppId", &value);
    uint8_t app_id = (uint8_t)value;

    btif_config_get_int(name, "HidVendorId", &value);
    dscp_info.vendor_id = (uint16_t)value;

    btif_config_get_int(name, "HidProductId", &value);
    dscp_info.product_id = (uint16_t)value;

    btif_config_get_int(name, "HidVersion", &value);
    dscp_info.version = (uint8_t)value;

    btif_config_get_int(name, "HidCountryCode", &value);
    dscp_info.ctry_code = (uint8_t)value;

    value = 0;
    btif_config_get_int(name, "HidSSRMaxLatency", &value);
    dscp_info.ssr_max_latency = (uint16_t)value;

    value = 0;
    btif_config_get_int(name, "HidSSRMinTimeout", &value);
    dscp_info.ssr_min_tout = (uint16_t)value;

    size_t len = btif_config_get_bin_length(name, "HidDescriptor");
    if (len > 0) {
      dscp_info.descriptor.dl_len = (uint16_t)len;
      dscp_info.descriptor.dsc_list = (uint8_t*)alloca(len);
      btif_config_get_bin(name, "HidDescriptor",
                          (uint8_t*)dscp_info.descriptor.dsc_list, &len);
    }

    bool reconnect_allowed = false;
    btif_storage_get_hid_connection_policy(bd_addr, &reconnect_allowed);

    // add extracted information to BTA HH
    if (btif_hh_add_added_dev(bd_addr, attr_mask, reconnect_allowed)) {
      BTA_HhAddDev(bd_addr, attr_mask, sub_class, app_id, dscp_info);
    }
  }

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         btif_storage_remove_hid_info
 *
 * Description      BTIF storage API - Deletes the bonded hid device info from
 *                  NVRAM
 *
 * Returns          BT_STATUS_SUCCESS if the deletion was successful,
 *                  BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_remove_hid_info(const RawAddress& remote_bd_addr) {
  std::string bdstr = remote_bd_addr.ToString();

  btif_config_remove(bdstr, "HidAttrMask");
  btif_config_remove(bdstr, "HidSubClass");
  btif_config_remove(bdstr, "HidAppId");
  btif_config_remove(bdstr, "HidVendorId");
  btif_config_remove(bdstr, "HidProductId");
  btif_config_remove(bdstr, "HidVersion");
  btif_config_remove(bdstr, "HidCountryCode");
  btif_config_remove(bdstr, "HidSSRMaxLatency");
  btif_config_remove(bdstr, "HidSSRMinTimeout");
  btif_config_remove(bdstr, "HidDescriptor");
  btif_config_remove(bdstr, BTIF_STORAGE_KEY_HID_RECONNECT_ALLOWED);
  btif_config_save();
  return BT_STATUS_SUCCESS;
}

constexpr char HEARING_AID_READ_PSM_HANDLE[] = "HearingAidReadPsmHandle";
constexpr char HEARING_AID_CAPABILITIES[] = "HearingAidCapabilities";
constexpr char HEARING_AID_CODECS[] = "HearingAidCodecs";
constexpr char HEARING_AID_AUDIO_CONTROL_POINT[] =
    "HearingAidAudioControlPoint";
constexpr char HEARING_AID_VOLUME_HANDLE[] = "HearingAidVolumeHandle";
constexpr char HEARING_AID_AUDIO_STATUS_HANDLE[] =
    "HearingAidAudioStatusHandle";
constexpr char HEARING_AID_AUDIO_STATUS_CCC_HANDLE[] =
    "HearingAidAudioStatusCccHandle";
constexpr char HEARING_AID_SERVICE_CHANGED_CCC_HANDLE[] =
    "HearingAidServiceChangedCccHandle";
constexpr char HEARING_AID_SYNC_ID[] = "HearingAidSyncId";
constexpr char HEARING_AID_RENDER_DELAY[] = "HearingAidRenderDelay";
constexpr char HEARING_AID_PREPARATION_DELAY[] = "HearingAidPreparationDelay";
constexpr char HEARING_AID_IS_ACCEPTLISTED[] = "HearingAidIsAcceptlisted";

void btif_storage_add_hearing_aid(const HearingDevice& dev_info) {
  do_in_jni_thread(
      FROM_HERE,
      Bind(
          [](const HearingDevice& dev_info) {
            std::string bdstr = dev_info.address.ToString();
            VLOG(2) << "saving hearing aid device: " << bdstr;
            btif_config_set_int(bdstr, HEARING_AID_SERVICE_CHANGED_CCC_HANDLE,
                                dev_info.service_changed_ccc_handle);
            btif_config_set_int(bdstr, HEARING_AID_READ_PSM_HANDLE,
                                dev_info.read_psm_handle);
            btif_config_set_int(bdstr, HEARING_AID_CAPABILITIES,
                                dev_info.capabilities);
            btif_config_set_int(bdstr, HEARING_AID_CODECS, dev_info.codecs);
            btif_config_set_int(bdstr, HEARING_AID_AUDIO_CONTROL_POINT,
                                dev_info.audio_control_point_handle);
            btif_config_set_int(bdstr, HEARING_AID_VOLUME_HANDLE,
                                dev_info.volume_handle);
            btif_config_set_int(bdstr, HEARING_AID_AUDIO_STATUS_HANDLE,
                                dev_info.audio_status_handle);
            btif_config_set_int(bdstr, HEARING_AID_AUDIO_STATUS_CCC_HANDLE,
                                dev_info.audio_status_ccc_handle);
            btif_config_set_uint64(bdstr, HEARING_AID_SYNC_ID,
                                   dev_info.hi_sync_id);
            btif_config_set_int(bdstr, HEARING_AID_RENDER_DELAY,
                                dev_info.render_delay);
            btif_config_set_int(bdstr, HEARING_AID_PREPARATION_DELAY,
                                dev_info.preparation_delay);
            btif_config_set_int(bdstr, HEARING_AID_IS_ACCEPTLISTED, true);
            btif_config_save();
          },
          dev_info));
}

/** Loads information about bonded hearing aid devices */
void btif_storage_load_bonded_hearing_aids() {
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    const std::string& name = bd_addr.ToString();

    int size = STORAGE_UUID_STRING_SIZE * HEARINGAID_MAX_NUM_UUIDS;
    char uuid_str[size];
    bool isHearingaidDevice = false;
    if (btif_config_get_str(name, BTIF_STORAGE_PATH_REMOTE_SERVICE, uuid_str,
                            &size)) {
      Uuid p_uuid[HEARINGAID_MAX_NUM_UUIDS];
      size_t num_uuids =
          btif_split_uuids_string(uuid_str, p_uuid, HEARINGAID_MAX_NUM_UUIDS);
      for (size_t i = 0; i < num_uuids; i++) {
        if (p_uuid[i] == Uuid::FromString("FDF0")) {
          isHearingaidDevice = true;
          break;
        }
      }
    }
    if (!isHearingaidDevice) {
      continue;
    }

    BTIF_TRACE_DEBUG("Remote device:%s", name.c_str());

    if (btif_in_fetch_bonded_device(name) != BT_STATUS_SUCCESS) {
      btif_storage_remove_hearing_aid(bd_addr);
      continue;
    }

    int value;
    uint8_t capabilities = 0;
    if (btif_config_get_int(name, HEARING_AID_CAPABILITIES, &value))
      capabilities = value;

    uint16_t codecs = 0;
    if (btif_config_get_int(name, HEARING_AID_CODECS, &value)) codecs = value;

    uint16_t audio_control_point_handle = 0;
    if (btif_config_get_int(name, HEARING_AID_AUDIO_CONTROL_POINT, &value))
      audio_control_point_handle = value;

    uint16_t audio_status_handle = 0;
    if (btif_config_get_int(name, HEARING_AID_AUDIO_STATUS_HANDLE, &value))
      audio_status_handle = value;

    uint16_t audio_status_ccc_handle = 0;
    if (btif_config_get_int(name, HEARING_AID_AUDIO_STATUS_CCC_HANDLE, &value))
      audio_status_ccc_handle = value;

    uint16_t service_changed_ccc_handle = 0;
    if (btif_config_get_int(name, HEARING_AID_SERVICE_CHANGED_CCC_HANDLE,
                            &value))
      service_changed_ccc_handle = value;

    uint16_t volume_handle = 0;
    if (btif_config_get_int(name, HEARING_AID_VOLUME_HANDLE, &value))
      volume_handle = value;

    uint16_t read_psm_handle = 0;
    if (btif_config_get_int(name, HEARING_AID_READ_PSM_HANDLE, &value))
      read_psm_handle = value;

    uint64_t lvalue;
    uint64_t hi_sync_id = 0;
    if (btif_config_get_uint64(name, HEARING_AID_SYNC_ID, &lvalue))
      hi_sync_id = lvalue;

    uint16_t render_delay = 0;
    if (btif_config_get_int(name, HEARING_AID_RENDER_DELAY, &value))
      render_delay = value;

    uint16_t preparation_delay = 0;
    if (btif_config_get_int(name, HEARING_AID_PREPARATION_DELAY, &value))
      preparation_delay = value;

    uint16_t is_acceptlisted = 0;
    if (btif_config_get_int(name, HEARING_AID_IS_ACCEPTLISTED, &value))
      is_acceptlisted = value;

    // add extracted information to BTA Hearing Aid
    do_in_main_thread(
        FROM_HERE,
        Bind(&HearingAid::AddFromStorage,
             HearingDevice(bd_addr, capabilities, codecs,
                           audio_control_point_handle, audio_status_handle,
                           audio_status_ccc_handle, service_changed_ccc_handle,
                           volume_handle, read_psm_handle, hi_sync_id,
                           render_delay, preparation_delay),
             is_acceptlisted));
  }
}

/** Deletes the bonded hearing aid device info from NVRAM */
void btif_storage_remove_hearing_aid(const RawAddress& address) {
  std::string addrstr = address.ToString();
  btif_config_remove(addrstr, HEARING_AID_READ_PSM_HANDLE);
  btif_config_remove(addrstr, HEARING_AID_CAPABILITIES);
  btif_config_remove(addrstr, HEARING_AID_CODECS);
  btif_config_remove(addrstr, HEARING_AID_AUDIO_CONTROL_POINT);
  btif_config_remove(addrstr, HEARING_AID_VOLUME_HANDLE);
  btif_config_remove(addrstr, HEARING_AID_AUDIO_STATUS_HANDLE);
  btif_config_remove(addrstr, HEARING_AID_AUDIO_STATUS_CCC_HANDLE);
  btif_config_remove(addrstr, HEARING_AID_SERVICE_CHANGED_CCC_HANDLE);
  btif_config_remove(addrstr, HEARING_AID_SYNC_ID);
  btif_config_remove(addrstr, HEARING_AID_RENDER_DELAY);
  btif_config_remove(addrstr, HEARING_AID_PREPARATION_DELAY);
  btif_config_remove(addrstr, HEARING_AID_IS_ACCEPTLISTED);
  btif_config_save();
}

/** Set/Unset the hearing aid device HEARING_AID_IS_ACCEPTLISTED flag. */
void btif_storage_set_hearing_aid_acceptlist(const RawAddress& address,
                                             bool add_to_acceptlist) {
  std::string addrstr = address.ToString();

  btif_config_set_int(addrstr, HEARING_AID_IS_ACCEPTLISTED, add_to_acceptlist);
  btif_config_save();
}

/** Get the hearing aid device properties. */
bool btif_storage_get_hearing_aid_prop(
    const RawAddress& address, uint8_t* capabilities, uint64_t* hi_sync_id,
    uint16_t* render_delay, uint16_t* preparation_delay, uint16_t* codecs) {
  std::string addrstr = address.ToString();

  int value;
  if (btif_config_get_int(addrstr, HEARING_AID_CAPABILITIES, &value)) {
    *capabilities = value;
  } else {
    return false;
  }

  if (btif_config_get_int(addrstr, HEARING_AID_CODECS, &value)) {
    *codecs = value;
  } else {
    return false;
  }

  if (btif_config_get_int(addrstr, HEARING_AID_RENDER_DELAY, &value)) {
    *render_delay = value;
  } else {
    return false;
  }

  if (btif_config_get_int(addrstr, HEARING_AID_PREPARATION_DELAY, &value)) {
    *preparation_delay = value;
  } else {
    return false;
  }

  uint64_t lvalue;
  if (btif_config_get_uint64(addrstr, HEARING_AID_SYNC_ID, &lvalue)) {
    *hi_sync_id = lvalue;
  } else {
    return false;
  }

  return true;
}

/** Stores information about GATT server supported features */
void btif_storage_set_gatt_sr_supp_feat(const RawAddress& addr, uint8_t feat) {
  do_in_jni_thread(
      FROM_HERE, Bind(
                     [](const RawAddress& addr, uint8_t feat) {
                       std::string bdstr = addr.ToString();
                       VLOG(2)
                           << "GATT server supported features for: " << bdstr
                           << " features: " << +feat;
                       btif_config_set_int(
                           bdstr, BTIF_STORAGE_KEY_GATT_SERVER_SUPPORTED, feat);
                       btif_config_save();
                     },
                     addr, feat));
}

/** Gets information about GATT server supported features */
uint8_t btif_storage_get_sr_supp_feat(const RawAddress& bd_addr) {
  auto name = bd_addr.ToString();

  int value = 0;
  btif_config_get_int(name, BTIF_STORAGE_KEY_GATT_SERVER_SUPPORTED, &value);
  BTIF_TRACE_DEBUG("Remote device: %s GATT server supported features 0x%02x",
                   name.c_str(), value);

  return value;
}

/** Set autoconnect information for LeAudio device */
void btif_storage_set_leaudio_autoconnect(const RawAddress& addr,
                                          bool autoconnect) {
  do_in_jni_thread(FROM_HERE, Bind(
                                  [](const RawAddress& addr, bool autoconnect) {
                                    std::string bdstr = addr.ToString();
                                    VLOG(2)
                                        << "saving le audio device: " << bdstr;
                                    btif_config_set_int(
                                        bdstr, BTIF_STORAGE_LEAUDIO_AUTOCONNECT,
                                        autoconnect);
                                    btif_config_save();
                                  },
                                  addr, autoconnect));
}

/** Store ASEs information */
void btif_storage_leaudio_update_handles_bin(const RawAddress& addr) {
  std::vector<uint8_t> handles;

  if (LeAudioClient::GetHandlesForStorage(addr, handles)) {
    do_in_jni_thread(
        FROM_HERE,
        Bind(
            [](const RawAddress& bd_addr, std::vector<uint8_t> handles) {
              auto bdstr = bd_addr.ToString();
              btif_config_set_bin(bdstr, BTIF_STORAGE_LEAUDIO_HANDLES_BIN,
                                  handles.data(), handles.size());
              btif_config_save();
            },
            addr, std::move(handles)));
  }
}

/** Store PACs information */
void btif_storage_leaudio_update_pacs_bin(const RawAddress& addr) {
  std::vector<uint8_t> sink_pacs;

  if (LeAudioClient::GetSinkPacsForStorage(addr, sink_pacs)) {
    do_in_jni_thread(
        FROM_HERE,
        Bind(
            [](const RawAddress& bd_addr, std::vector<uint8_t> sink_pacs) {
              auto bdstr = bd_addr.ToString();
              btif_config_set_bin(bdstr, BTIF_STORAGE_LEAUDIO_SINK_PACS_BIN,
                                  sink_pacs.data(), sink_pacs.size());
              btif_config_save();
            },
            addr, std::move(sink_pacs)));
  }

  std::vector<uint8_t> source_pacs;
  if (LeAudioClient::GetSourcePacsForStorage(addr, source_pacs)) {
    do_in_jni_thread(
        FROM_HERE,
        Bind(
            [](const RawAddress& bd_addr, std::vector<uint8_t> source_pacs) {
              auto bdstr = bd_addr.ToString();
              btif_config_set_bin(bdstr, BTIF_STORAGE_LEAUDIO_SOURCE_PACS_BIN,
                                  source_pacs.data(), source_pacs.size());
              btif_config_save();
            },
            addr, std::move(source_pacs)));
  }
}

/** Store ASEs information */
void btif_storage_leaudio_update_ase_bin(const RawAddress& addr) {
  std::vector<uint8_t> ases;

  if (LeAudioClient::GetAsesForStorage(addr, ases)) {
    do_in_jni_thread(
        FROM_HERE,
        Bind(
            [](const RawAddress& bd_addr, std::vector<uint8_t> ases) {
              auto bdstr = bd_addr.ToString();
              btif_config_set_bin(bdstr, BTIF_STORAGE_LEAUDIO_ASES_BIN,
                                  ases.data(), ases.size());
              btif_config_save();
            },
            addr, std::move(ases)));
  }
}

/** Store Le Audio device audio locations */
void btif_storage_set_leaudio_audio_location(const RawAddress& addr,
                                             uint32_t sink_location,
                                             uint32_t source_location) {
  do_in_jni_thread(
      FROM_HERE,
      Bind(
          [](const RawAddress& addr, int sink_location, int source_location) {
            std::string bdstr = addr.ToString();
            LOG_DEBUG("saving le audio device: %s", bdstr.c_str());
            btif_config_set_int(bdstr, BTIF_STORAGE_LEAUDIO_SINK_AUDIOLOCATION,
                                sink_location);
            btif_config_set_int(bdstr,
                                BTIF_STORAGE_LEAUDIO_SOURCE_AUDIOLOCATION,
                                source_location);
            btif_config_save();
          },
          addr, sink_location, source_location));
}

/** Store Le Audio device context types */
void btif_storage_set_leaudio_supported_context_types(
    const RawAddress& addr, uint16_t sink_supported_context_type,
    uint16_t source_supported_context_type) {
  do_in_jni_thread(
      FROM_HERE,
      Bind(
          [](const RawAddress& addr, int sink_supported_context_type,
             int source_supported_context_type) {
            std::string bdstr = addr.ToString();
            LOG_DEBUG("saving le audio device: %s", bdstr.c_str());
            btif_config_set_int(
                bdstr, BTIF_STORAGE_LEAUDIO_SINK_SUPPORTED_CONTEXT_TYPE,
                sink_supported_context_type);
            btif_config_set_int(
                bdstr, BTIF_STORAGE_LEAUDIO_SOURCE_SUPPORTED_CONTEXT_TYPE,
                source_supported_context_type);
            btif_config_save();
          },
          addr, sink_supported_context_type, source_supported_context_type));
}

/** Loads information about bonded Le Audio devices */
void btif_storage_load_bonded_leaudio() {
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    auto name = bd_addr.ToString();

    int size = STORAGE_UUID_STRING_SIZE * BT_MAX_NUM_UUIDS;
    char uuid_str[size];
    bool isLeAudioDevice = false;
    if (btif_config_get_str(name, BTIF_STORAGE_PATH_REMOTE_SERVICE, uuid_str,
                            &size)) {
      Uuid p_uuid[BT_MAX_NUM_UUIDS];
      size_t num_uuids =
          btif_split_uuids_string(uuid_str, p_uuid, BT_MAX_NUM_UUIDS);
      for (size_t i = 0; i < num_uuids; i++) {
        if (p_uuid[i] == Uuid::FromString("184E")) {
          isLeAudioDevice = true;
          break;
        }
      }
    }
    if (!isLeAudioDevice) {
      continue;
    }

    BTIF_TRACE_DEBUG("Remote device:%s", name.c_str());

    int value;
    bool autoconnect = false;
    if (btif_config_get_int(name, BTIF_STORAGE_LEAUDIO_AUTOCONNECT, &value))
      autoconnect = !!value;

    int sink_audio_location = 0;
    if (btif_config_get_int(name, BTIF_STORAGE_LEAUDIO_SINK_AUDIOLOCATION,
                            &value))
      sink_audio_location = value;

    int source_audio_location = 0;
    if (btif_config_get_int(name, BTIF_STORAGE_LEAUDIO_SOURCE_AUDIOLOCATION,
                            &value))
      source_audio_location = value;

    int sink_supported_context_type = 0;
    if (btif_config_get_int(
            name, BTIF_STORAGE_LEAUDIO_SINK_SUPPORTED_CONTEXT_TYPE, &value))
      sink_supported_context_type = value;

    int source_supported_context_type = 0;
    if (btif_config_get_int(
            name, BTIF_STORAGE_LEAUDIO_SOURCE_SUPPORTED_CONTEXT_TYPE, &value))
      source_supported_context_type = value;

    size_t buffer_size =
        btif_config_get_bin_length(name, BTIF_STORAGE_LEAUDIO_HANDLES_BIN);
    std::vector<uint8_t> handles(buffer_size);
    if (buffer_size > 0) {
      btif_config_get_bin(name, BTIF_STORAGE_LEAUDIO_HANDLES_BIN,
                          handles.data(), &buffer_size);
    }

    buffer_size =
        btif_config_get_bin_length(name, BTIF_STORAGE_LEAUDIO_SINK_PACS_BIN);
    std::vector<uint8_t> sink_pacs(buffer_size);
    if (buffer_size > 0) {
      btif_config_get_bin(name, BTIF_STORAGE_LEAUDIO_SINK_PACS_BIN,
                          sink_pacs.data(), &buffer_size);
    }

    buffer_size =
        btif_config_get_bin_length(name, BTIF_STORAGE_LEAUDIO_SOURCE_PACS_BIN);
    std::vector<uint8_t> source_pacs(buffer_size);
    if (buffer_size > 0) {
      btif_config_get_bin(name, BTIF_STORAGE_LEAUDIO_SOURCE_PACS_BIN,
                          source_pacs.data(), &buffer_size);
    }

    buffer_size =
        btif_config_get_bin_length(name, BTIF_STORAGE_LEAUDIO_ASES_BIN);
    std::vector<uint8_t> ases(buffer_size);
    if (buffer_size > 0) {
      btif_config_get_bin(name, BTIF_STORAGE_LEAUDIO_ASES_BIN, ases.data(),
                          &buffer_size);
    }

    do_in_main_thread(
        FROM_HERE,
        Bind(&LeAudioClient::AddFromStorage, bd_addr, autoconnect,
             sink_audio_location, source_audio_location,
             sink_supported_context_type, source_supported_context_type,
             std::move(handles), std::move(sink_pacs), std::move(source_pacs),
             std::move(ases)));
  }
}

/** Remove the Le Audio device from storage */
void btif_storage_remove_leaudio(const RawAddress& address) {
  std::string addrstr = address.ToString();
  btif_config_set_int(addrstr, BTIF_STORAGE_LEAUDIO_AUTOCONNECT, false);
}

constexpr char HAS_IS_ACCEPTLISTED[] = "LeAudioHasIsAcceptlisted";
constexpr char HAS_FEATURES[] = "LeAudioHasFlags";
constexpr char HAS_ACTIVE_PRESET[] = "LeAudioHasActivePreset";
constexpr char HAS_SERIALIZED_PRESETS[] = "LeAudioHasSerializedPresets";

void btif_storage_add_leaudio_has_device(const RawAddress& address,
                                         std::vector<uint8_t> presets_bin,
                                         uint8_t features,
                                         uint8_t active_preset) {
  do_in_jni_thread(
      FROM_HERE,
      Bind(
          [](const RawAddress& address, std::vector<uint8_t> presets_bin,
             uint8_t features, uint8_t active_preset) {
            const std::string& name = address.ToString();

            btif_config_set_int(name, HAS_FEATURES, features);
            btif_config_set_int(name, HAS_ACTIVE_PRESET, active_preset);
            btif_config_set_bin(name, HAS_SERIALIZED_PRESETS,
                                presets_bin.data(), presets_bin.size());

            btif_config_set_int(name, HAS_IS_ACCEPTLISTED, true);
            btif_config_save();
          },
          address, std::move(presets_bin), features, active_preset));
}

void btif_storage_set_leaudio_has_active_preset(const RawAddress& address,
                                                uint8_t active_preset) {
  do_in_jni_thread(FROM_HERE,
                   Bind(
                       [](const RawAddress& address, uint8_t active_preset) {
                         const std::string& name = address.ToString();

                         btif_config_set_int(name, HAS_ACTIVE_PRESET,
                                             active_preset);
                         btif_config_save();
                       },
                       address, active_preset));
}

bool btif_storage_get_leaudio_has_features(const RawAddress& address,
                                           uint8_t& features) {
  std::string name = address.ToString();

  int value;
  if (!btif_config_get_int(name, HAS_FEATURES, &value)) return false;

  features = value;
  return true;
}

void btif_storage_set_leaudio_has_features(const RawAddress& address,
                                           uint8_t features) {
  do_in_jni_thread(FROM_HERE,
                   Bind(
                       [](const RawAddress& address, uint8_t features) {
                         const std::string& name = address.ToString();

                         btif_config_set_int(name, HAS_FEATURES, features);
                         btif_config_save();
                       },
                       address, features));
}

void btif_storage_load_bonded_leaudio_has_devices() {
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    const std::string& name = bd_addr.ToString();

    if (!btif_config_exist(name, HAS_IS_ACCEPTLISTED) &&
        !btif_config_exist(name, HAS_FEATURES))
      continue;

    int value;
    uint16_t is_acceptlisted = 0;
    if (btif_config_get_int(name, HAS_IS_ACCEPTLISTED, &value))
      is_acceptlisted = value;

    uint8_t features = 0;
    if (btif_config_get_int(name, HAS_FEATURES, &value)) features = value;

#ifndef TARGET_FLOSS
    do_in_main_thread(FROM_HERE, Bind(&le_audio::has::HasClient::AddFromStorage,
                                      bd_addr, features, is_acceptlisted));
#else
    ASSERT_LOG(false, "TODO - Fix LE audio build.");
#endif
  }
}

void btif_storage_remove_leaudio_has(const RawAddress& address) {
  std::string addrstr = address.ToString();
  btif_config_remove(addrstr, HAS_IS_ACCEPTLISTED);
  btif_config_remove(addrstr, HAS_FEATURES);
  btif_config_remove(addrstr, HAS_ACTIVE_PRESET);
  btif_config_remove(addrstr, HAS_SERIALIZED_PRESETS);
  btif_config_save();
}

void btif_storage_set_leaudio_has_acceptlist(const RawAddress& address,
                                             bool add_to_acceptlist) {
  std::string addrstr = address.ToString();

  btif_config_set_int(addrstr, HAS_IS_ACCEPTLISTED, add_to_acceptlist);
  btif_config_save();
}

void btif_storage_set_leaudio_has_presets(const RawAddress& address,
                                          std::vector<uint8_t> presets_bin) {
  do_in_jni_thread(
      FROM_HERE,
      Bind(
          [](const RawAddress& address, std::vector<uint8_t> presets_bin) {
            const std::string& name = address.ToString();

            btif_config_set_bin(name, HAS_SERIALIZED_PRESETS,
                                presets_bin.data(), presets_bin.size());
            btif_config_save();
          },
          address, std::move(presets_bin)));
}

bool btif_storage_get_leaudio_has_presets(const RawAddress& address,
                                          std::vector<uint8_t>& presets_bin,
                                          uint8_t& active_preset) {
  std::string name = address.ToString();

  int value;
  if (!btif_config_get_int(name, HAS_ACTIVE_PRESET, &value)) return false;
  active_preset = value;

  auto bin_sz = btif_config_get_bin_length(name, HAS_SERIALIZED_PRESETS);
  presets_bin.resize(bin_sz);
  if (!btif_config_get_bin(name, HAS_SERIALIZED_PRESETS, presets_bin.data(),
                           &bin_sz))
    return false;

  return true;
}

/** Adds the bonded Le Audio device grouping info into the NVRAM */
void btif_storage_add_groups(const RawAddress& addr) {
  std::vector<uint8_t> group_info;
  auto not_empty = DeviceGroups::GetForStorage(addr, group_info);

  if (not_empty)
    do_in_jni_thread(
        FROM_HERE,
        Bind(
            [](const RawAddress& bd_addr, std::vector<uint8_t> group_info) {
              auto bdstr = bd_addr.ToString();
              btif_config_set_bin(bdstr, BTIF_STORAGE_DEVICE_GROUP_BIN,
                                  group_info.data(), group_info.size());
              btif_config_save();
            },
            addr, std::move(group_info)));
}

/** Deletes the bonded Le Audio device grouping info from the NVRAM */
void btif_storage_remove_groups(const RawAddress& address) {
  std::string addrstr = address.ToString();
  btif_config_remove(addrstr, BTIF_STORAGE_DEVICE_GROUP_BIN);
  btif_config_save();
}

/** Loads information about bonded group devices */
void btif_storage_load_bonded_groups(void) {
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    auto name = bd_addr.ToString();
    size_t buffer_size =
        btif_config_get_bin_length(name, BTIF_STORAGE_DEVICE_GROUP_BIN);
    if (buffer_size == 0) continue;

    BTIF_TRACE_DEBUG("Grouped device:%s", name.c_str());

    std::vector<uint8_t> in(buffer_size);
    if (btif_config_get_bin(name, BTIF_STORAGE_DEVICE_GROUP_BIN, in.data(),
                            &buffer_size)) {
      do_in_main_thread(FROM_HERE, Bind(&DeviceGroups::AddFromStorage, bd_addr,
                                        std::move(in)));
    }
  }
}

void btif_storage_set_csis_autoconnect(const RawAddress& addr,
                                       bool autoconnect) {
  do_in_jni_thread(FROM_HERE, Bind(
                                  [](const RawAddress& addr, bool autoconnect) {
                                    std::string bdstr = addr.ToString();
                                    VLOG(2) << "Storing CSIS device: " << bdstr;
                                    btif_config_set_int(
                                        bdstr, BTIF_STORAGE_CSIS_AUTOCONNECT,
                                        autoconnect);
                                    btif_config_save();
                                  },
                                  addr, autoconnect));
}

/** Stores information about the bonded CSIS device */
void btif_storage_update_csis_info(const RawAddress& addr) {
  std::vector<uint8_t> set_info;
  auto not_empty = CsisClient::GetForStorage(addr, set_info);

  if (not_empty)
    do_in_jni_thread(
        FROM_HERE,
        Bind(
            [](const RawAddress& bd_addr, std::vector<uint8_t> set_info) {
              auto bdstr = bd_addr.ToString();
              btif_config_set_bin(bdstr, BTIF_STORAGE_CSIS_SET_INFO_BIN,
                                  set_info.data(), set_info.size());
              btif_config_save();
            },
            addr, std::move(set_info)));
}

/** Loads information about the bonded CSIS device */
void btif_storage_load_bonded_csis_devices(void) {
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    auto name = bd_addr.ToString();

    BTIF_TRACE_DEBUG("Loading CSIS device:%s", name.c_str());

    int value;
    bool autoconnect = false;
    if (btif_config_get_int(name, BTIF_STORAGE_CSIS_AUTOCONNECT, &value))
      autoconnect = !!value;

    size_t buffer_size =
        btif_config_get_bin_length(name, BTIF_STORAGE_CSIS_SET_INFO_BIN);
    std::vector<uint8_t> in(buffer_size);
    if (buffer_size != 0)
      btif_config_get_bin(name, BTIF_STORAGE_CSIS_SET_INFO_BIN, in.data(),
                          &buffer_size);

    if (buffer_size != 0 || autoconnect)
      do_in_main_thread(FROM_HERE, Bind(&CsisClient::AddFromStorage, bd_addr,
                                        std::move(in), autoconnect));
  }
}

/** Removes information about the bonded CSIS device */
void btif_storage_remove_csis_device(const RawAddress& address) {
  std::string addrstr = address.ToString();
  btif_config_remove(addrstr, BTIF_STORAGE_CSIS_AUTOCONNECT);
  btif_config_remove(addrstr, BTIF_STORAGE_CSIS_SET_INFO_BIN);
  btif_config_save();
}

/*******************************************************************************
 *
 * Function         btif_storage_is_restricted_device
 *
 * Description      BTIF storage API - checks if this device is a restricted
 *                  device
 *
 * Returns          true  if the device is labeled as restricted
 *                  false otherwise
 *
 ******************************************************************************/
bool btif_storage_is_restricted_device(const RawAddress* remote_bd_addr) {
  return btif_config_exist(remote_bd_addr->ToString(), "Restricted");
}

int btif_storage_get_num_bonded_devices(void) {
  btif_bonded_devices_t bonded_devices;
  btif_in_fetch_bonded_devices(&bonded_devices, 0);
  return bonded_devices.num_devices;
}

/*******************************************************************************
 * Function         btif_storage_load_hidd
 *
 * Description      Loads hidd bonded device and "plugs" it into hidd
 *
 * Returns          BT_STATUS_SUCCESS if successful, BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
bt_status_t btif_storage_load_hidd(void) {
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    auto name = bd_addr.ToString();

    BTIF_TRACE_DEBUG("Remote device:%s", name.c_str());
    int value;
    if (btif_in_fetch_bonded_device(name) == BT_STATUS_SUCCESS) {
      if (btif_config_get_int(name, "HidDeviceCabled", &value)) {
        BTA_HdAddDevice(bd_addr);
        break;
      }
    }
  }

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         btif_storage_set_hidd
 *
 * Description      Stores currently used HIDD device info in nvram and remove
 *                  the "HidDeviceCabled" flag from unused devices
 *
 * Returns          BT_STATUS_SUCCESS
 *
 ******************************************************************************/
bt_status_t btif_storage_set_hidd(const RawAddress& remote_bd_addr) {
  std::string remote_device_address_string = remote_bd_addr.ToString();
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    auto name = bd_addr.ToString();
    if (bd_addr == remote_bd_addr) continue;
    if (btif_in_fetch_bonded_device(name) == BT_STATUS_SUCCESS) {
      btif_config_remove(name, "HidDeviceCabled");
    }
  }

  btif_config_set_int(remote_device_address_string, "HidDeviceCabled", 1);
  btif_config_save();
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         btif_storage_remove_hidd
 *
 * Description      Removes hidd bonded device info from nvram
 *
 * Returns          BT_STATUS_SUCCESS
 *
 ******************************************************************************/
bt_status_t btif_storage_remove_hidd(RawAddress* remote_bd_addr) {
  btif_config_remove(remote_bd_addr->ToString(), "HidDeviceCabled");
  btif_config_save();

  return BT_STATUS_SUCCESS;
}

// Get the name of a device from btif for interop database matching.
bool btif_storage_get_stored_remote_name(const RawAddress& bd_addr,
                                         char* name) {
  bt_property_t property;
  property.type = BT_PROPERTY_BDNAME;
  property.len = BTM_MAX_REM_BD_NAME_LEN;
  property.val = name;

  return (btif_storage_get_remote_device_property(&bd_addr, &property) ==
          BT_STATUS_SUCCESS);
}

/** Stores information about GATT Client supported features support */
void btif_storage_set_gatt_cl_supp_feat(const RawAddress& bd_addr,
                                        uint8_t feat) {
  do_in_jni_thread(
      FROM_HERE, Bind(
                     [](const RawAddress& bd_addr, uint8_t feat) {
                       std::string bdstr = bd_addr.ToString();
                       VLOG(2)
                           << "saving gatt client supported feat: " << bdstr;
                       btif_config_set_int(
                           bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_SUPPORTED, feat);
                       btif_config_save();
                     },
                     bd_addr, feat));
}

/** Get client supported features */
uint8_t btif_storage_get_gatt_cl_supp_feat(const RawAddress& bd_addr) {
  auto name = bd_addr.ToString();

  int value = 0;
  btif_config_get_int(name, BTIF_STORAGE_KEY_GATT_CLIENT_SUPPORTED, &value);
  BTIF_TRACE_DEBUG("Remote device: %s GATT client supported features 0x%02x",
                   name.c_str(), value);

  return value;
}

/** Remove client supported features */
void btif_storage_remove_gatt_cl_supp_feat(const RawAddress& bd_addr) {
  do_in_jni_thread(
      FROM_HERE, Bind(
                     [](const RawAddress& bd_addr) {
                       auto bdstr = bd_addr.ToString();
                       if (btif_config_exist(
                               bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_SUPPORTED)) {
                         btif_config_remove(
                             bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_SUPPORTED);
                         btif_config_save();
                       }
                     },
                     bd_addr));
}

/** Store last server database hash for remote client */
void btif_storage_set_gatt_cl_db_hash(const RawAddress& bd_addr, Octet16 hash) {
  do_in_jni_thread(FROM_HERE, Bind(
                                  [](const RawAddress& bd_addr, Octet16 hash) {
                                    auto bdstr = bd_addr.ToString();
                                    btif_config_set_bin(
                                        bdstr,
                                        BTIF_STORAGE_KEY_GATT_CLIENT_DB_HASH,
                                        hash.data(), hash.size());
                                    btif_config_save();
                                  },
                                  bd_addr, hash));
}

/** Get last server database hash for remote client */
Octet16 btif_storage_get_gatt_cl_db_hash(const RawAddress& bd_addr) {
  auto bdstr = bd_addr.ToString();

  Octet16 hash;
  size_t size = hash.size();
  btif_config_get_bin(bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_DB_HASH, hash.data(),
                      &size);

  return hash;
}

/** Remove las server database hash for remote client */
void btif_storage_remove_gatt_cl_db_hash(const RawAddress& bd_addr) {
  do_in_jni_thread(FROM_HERE,
                   Bind(
                       [](const RawAddress& bd_addr) {
                         auto bdstr = bd_addr.ToString();
                         if (btif_config_exist(
                                 bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_DB_HASH)) {
                           btif_config_remove(
                               bdstr, BTIF_STORAGE_KEY_GATT_CLIENT_DB_HASH);
                           btif_config_save();
                         }
                       },
                       bd_addr));
}

void btif_debug_linkkey_type_dump(int fd) {
  dprintf(fd, "\nLink Key Types:\n");
  for (const auto& bd_addr : btif_config_get_paired_devices()) {
    auto bdstr = bd_addr.ToString();
    int linkkey_type;
    dprintf(fd, "  %s\n", bdstr.c_str());

    dprintf(fd, "    BR: ");
    if (btif_config_get_int(bdstr, "LinkKeyType", &linkkey_type)) {
      dprintf(fd, "%s", linkkey_type_text(linkkey_type).c_str());
    }
    dprintf(fd, "\n");

    dprintf(fd, "    LE:");
    if (btif_config_exist(bdstr, "LE_KEY_PENC")) dprintf(fd, " PENC");
    if (btif_config_exist(bdstr, "LE_KEY_PID")) dprintf(fd, " PID");
    if (btif_config_exist(bdstr, "LE_KEY_PCSRK")) dprintf(fd, " PCSRK");
    if (btif_config_exist(bdstr, "LE_KEY_PLK")) dprintf(fd, " PLK");
    if (btif_config_exist(bdstr, "LE_KEY_LENC")) dprintf(fd, " LENC");
    if (btif_config_exist(bdstr, "LE_KEY_LCSRK")) dprintf(fd, " LCSRK");
    if (btif_config_exist(bdstr, "LE_KEY_LID")) dprintf(fd, " LID");
    if (btif_config_exist(bdstr, "LE_KEY_PLK")) dprintf(fd, " LLK");

    dprintf(fd, "\n");
  }
}
