// #include "mesh.h"


// struct bt_mesh_cfg_app_key_list app_keys;


// void get_app_keys(struct bt_mesh_cfg_app_key_list* app_keys){
//     uint16_t net_idx = bt_mesh_primary_net_idx();
//     uint16_t addr = bt_mesh_primary_addr();
//     int err;

//     err = bt_mesh_cfg_app_key_get(net_idx, addr, net_idx, app_keys, NULL);
//     if (err) {
//         printk("Unable to get Application Keys (err %d)\n", err);
//         return;
//     }

//     for (int i = 0; i < app_keys->count; i++) {
//         printk("AppKey Index: %u\n", app_keys->keys[i]);
//     }
// }