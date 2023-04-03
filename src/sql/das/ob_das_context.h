/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef DEV_SRC_SQL_DAS_OB_DAS_CONTEXT_H_
#define DEV_SRC_SQL_DAS_OB_DAS_CONTEXT_H_
#include "sql/das/ob_das_define.h"
#include "sql/das/ob_das_location_router.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/das/ob_das_factory.h"
#include "storage/tx/ob_trans_define.h"
#include "sql/engine/dml/ob_dml_ctx_define.h"
namespace oceanbase
{
namespace sql
{
class ObDASTabletMapper;

struct DmlRowkeyDistCtx
{
public:
  DmlRowkeyDistCtx()
    : deleted_rows_(nullptr),
    table_id_(common::OB_INVALID_ID)
  {}
  SeRowkeyDistCtx *deleted_rows_;
  uint64_t table_id_;
};
typedef common::ObList<DmlRowkeyDistCtx, common::ObIAllocator> DASDelCtxList;

class ObDASCtx
{
  OB_UNIS_VERSION(1);
public:
  ObDASCtx(common::ObIAllocator &allocator)
    : table_locs_(allocator),
      schema_guard_(nullptr),
      location_router_(allocator),
      das_factory_(allocator),
      related_tablet_map_(allocator),
      allocator_(allocator),
      self_schema_guard_(false),
      snapshot_(),
      savepoint_(0),
      del_ctx_list_(allocator),
      jump_read_group_id_(-1),
      flags_(0)
  {
    need_check_server_ = 1;
    same_server_ = 1;
  }
  ~ObDASCtx()
  {
    if (schema_guard_ != nullptr && self_schema_guard_) {
      schema_guard_->~ObSchemaGetterGuard();
      schema_guard_ = nullptr;
    }
  }

  int init(const ObPhysicalPlan &plan, ObExecContext &ctx);
  ObDASTableLoc *get_table_loc_by_id(uint64_t table_loc_id, uint64_t ref_table_id);
  DASTableLocList &get_table_loc_list() { return table_locs_; }
  DASDelCtxList& get_das_del_ctx_list() {return  del_ctx_list_;}
  int extended_tablet_loc(ObDASTableLoc &table_loc,
                          const common::ObTabletID &tablet_id,
                          ObDASTabletLoc *&tablet_loc);
  int extended_tablet_loc(ObDASTableLoc &table_loc,
                          const ObCandiTabletLoc &candi_tablet_loc,
                          ObDASTabletLoc *&talet_loc);
  int refresh_tablet_loc(ObDASTabletLoc &tablet_loc);
  int extended_table_loc(const ObDASTableLocMeta &loc_meta, ObDASTableLoc *&table_loc);
  int add_candi_table_loc(const ObDASTableLocMeta &loc_meta, const ObCandiTableLoc &candi_table_loc);
  int get_das_tablet_mapper(const uint64_t ref_table_id,
                            ObDASTabletMapper &tablet_mapper,
                            const DASTableIDArrayWrap *related_table_ids = nullptr);
  bool has_same_lsid(share::ObLSID *lsid);
  int64_t get_related_tablet_cnt() const;
  void set_snapshot(const transaction::ObTxReadSnapshot &snapshot) { snapshot_ = snapshot; }
  transaction::ObTxReadSnapshot &get_snapshot() { return snapshot_; }
  int64_t get_savepoint() const { return savepoint_; }
  void set_savepoint(const int64_t savepoint) { savepoint_ = savepoint; }
  ObDASLocationRouter &get_location_router() { return location_router_; }
  int build_related_tablet_loc(ObDASTabletLoc &tablet_loc);
  int build_related_table_loc(ObDASTableLoc &table_loc);
  int rebuild_tablet_loc_reference();
  void clear_all_location_info()
  {
    table_locs_.clear();
    related_tablet_map_.clear();
  }
  ObDASTaskFactory &get_das_factory() { return das_factory_; }
  ObSchemaGetterGuard *&get_schema_guard() { return schema_guard_; }
  DASRelatedTabletMap &get_related_tablet_map() { return related_tablet_map_; }
  bool is_partition_hit();
  void unmark_need_check_server();

  TO_STRING_KV(K_(table_locs),
               K_(is_fk_cascading),
               K_(snapshot),
               K_(savepoint));
private:
  int check_same_server(const ObDASTabletLoc *tablet_loc);
private:
  DASTableLocList table_locs_;
  share::schema::ObSchemaGetterGuard *schema_guard_;
  ObDASLocationRouter location_router_;
  ObDASTaskFactory das_factory_;
  DASRelatedTabletMap related_tablet_map_;
  common::ObIAllocator &allocator_;
  bool self_schema_guard_;
  transaction::ObTxReadSnapshot snapshot_;           // Mvcc snapshot
  int64_t savepoint_;                                // DML savepoint
  //@todo: save snapshot version
  DASDelCtxList del_ctx_list_;
public:
  int64_t jump_read_group_id_;
  union {
    uint64_t flags_;
    struct {
      uint64_t is_fk_cascading_                 : 1; //fk starts to trigger nested sql
      uint64_t need_check_server_               : 1; //need to check if partitions hit the same server
      uint64_t same_server_                     : 1; //if partitions hit the same server, could be local or remote
      uint64_t reserved_                        : 62;
    };
  };

};
}  // namespace sql
}  // namespace oceanbase
#endif /* DEV_SRC_SQL_DAS_OB_DAS_CONTEXT_H_ */
