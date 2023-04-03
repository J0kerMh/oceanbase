static idm_item_t* idm_locate(idm_t* idm, uint64_t id) { return idm->table + (id % idm->capacity); }
static void idm_item_recycle(idm_item_t* item, uint64_t capacity) {
  item->data = NULL;
  item->id += capacity;
}

extern void* idm_get(idm_t* idm, uint64_t id);
void idm_init(idm_t* idm, int64_t capacity) {
  idm->capacity = capacity;
  memset(idm->table, 0, sizeof(idm_item_t) * capacity);
  for(int64_t i = 0; i < capacity; i++) {
    idm_item_t* pi = idm_locate(idm, i);
    pi->id = i;
    link_insert(&idm->free_list, &pi->link);
  }
}

uint64_t idm_set(idm_t* idm, void* data) {
  link_t* link = link_pop(&idm->free_list);
  if (link) {
    idm_item_t* item = structof(link, idm_item_t, link);
    item->data = data;
    return item->id;
  }
  return UINT64_MAX;
}

void idm_del(idm_t* idm, uint64_t id) {
  idm_item_t* item = idm_locate(idm, id);
  idm_item_recycle(item, idm->capacity);
  link_insert(&idm->free_list, &item->link);
}
