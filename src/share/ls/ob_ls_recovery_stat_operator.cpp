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

#define USING_LOG_PREFIX SHARE

#include "ob_ls_recovery_stat_operator.h"

#include "share/ob_errno.h"
#include "share/config/ob_server_config.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "lib/string/ob_sql_string.h"//ObSqlString
#include "lib/mysqlclient/ob_mysql_transaction.h"//ObMySQLTransaction
#include "common/ob_timeout_ctx.h"
#include "share/ob_share_util.h"
#include "share/ls/ob_ls_status_operator.h"
#include "share/scn.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::palf;
namespace oceanbase
{
namespace share
{
////////////////////ObLSRecoveryStat/////////////////////
bool ObLSRecoveryStat::is_valid() const
{
  return OB_INVALID_TENANT_ID != tenant_id_ && ls_id_.is_valid()
    && sync_scn_.is_valid()
    && readable_scn_.is_valid()
    && create_scn_.is_valid()
    && drop_scn_.is_valid();
}
int ObLSRecoveryStat::init(const uint64_t tenant_id,
           const ObLSID &id,
           const SCN &sync_scn,
           const SCN &readable_scn,
           const SCN &create_scn,
           const SCN &drop_scn)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id || !id.is_valid()
                  || !sync_scn.is_valid()
                  || !readable_scn.is_valid()
                  || !create_scn.is_valid()
                  || !drop_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(id),
             K(sync_scn), K(readable_scn), K(create_scn), K(drop_scn));
  } else {
    tenant_id_ = tenant_id;
    ls_id_ = id;
    sync_scn_ = sync_scn;
    readable_scn_ = readable_scn;
    create_scn_ = create_scn;
    drop_scn_ = drop_scn;
  }
  return ret;

}

int ObLSRecoveryStat::init_only_recovery_stat(const uint64_t tenant_id, const ObLSID &id,
                                              const SCN &sync_scn, const SCN &readable_scn)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id
                  || !id.is_valid()
                  || !sync_scn.is_valid()
                  || !readable_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(id),
             K(sync_scn), K(readable_scn));
  } else {
    tenant_id_ = tenant_id;
    ls_id_ = id;
    sync_scn_ = sync_scn;
    readable_scn_ = readable_scn;
    drop_scn_ = SCN::base_scn();
    create_scn_ = SCN::base_scn();
  }
  return ret;
}
void ObLSRecoveryStat::reset()
{
  tenant_id_ = OB_INVALID_TENANT_ID;
  ls_id_.reset();
  sync_scn_.reset();
  readable_scn_.reset();
  create_scn_.reset();
  drop_scn_.reset();
}

int ObLSRecoveryStat::assign(const ObLSRecoveryStat &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    tenant_id_ = other.tenant_id_;
    ls_id_ = other.ls_id_;
    sync_scn_ = other.sync_scn_;
    readable_scn_ = other.readable_scn_;
    create_scn_ = other.create_scn_;
    drop_scn_ = other.drop_scn_;
  }
  return ret;
}
////////////////////ObLSRecoveryStatOperator/////////////
int ObLSRecoveryStatOperator::create_new_ls(const ObLSStatusInfo &ls_info,
                                            const SCN &create_ls_scn,
                                            const common::ObString &zone_priority,
                                            const share::ObTenantSwitchoverStatus &working_sw_status,
                                            ObMySQLTransaction &trans)
{
  UNUSEDx(zone_priority, working_sw_status);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_info.is_valid() || !create_ls_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(ls_info));
  } else {
    common::ObSqlString sql;
    SCN init_scn = SCN::base_scn();
    const uint64_t init_scn_value = init_scn.get_val_for_inner_table_field();
    if (OB_FAIL(sql.assign_fmt(
            "INSERT into %s (tenant_id, ls_id, create_scn, "
            "sync_scn, readable_scn, drop_scn) "
            "values (%lu, %ld, '%lu', '%lu', '%lu', '%lu')",
            OB_ALL_LS_RECOVERY_STAT_TNAME, ls_info.tenant_id_,
            ls_info.ls_id_.id(), create_ls_scn.get_val_for_inner_table_field(),
            init_scn_value, init_scn_value, init_scn_value))) {
      LOG_WARN("failed to assing sql", KR(ret), K(ls_info), K(create_ls_scn), K(init_scn_value));
    } else if (OB_FAIL(exec_write(ls_info.tenant_id_, sql, this, trans))) {
      LOG_WARN("failed to exec write", KR(ret), K(ls_info), K(sql));
    }
    LOG_INFO("[LS_RECOVERY] create new ls", KR(ret), K(ls_info), K(create_ls_scn), K(init_scn));
  }
  return ret;
}

int ObLSRecoveryStatOperator::drop_ls(const uint64_t &tenant_id,
                                      const share::ObLSID &ls_id,
                                      const ObTenantSwitchoverStatus &working_sw_status,
                                      ObMySQLTransaction &trans)
{
  UNUSEDx(working_sw_status);
  int ret = OB_SUCCESS;
  ObLSRecoveryStat old_ls_recovery;
  if (OB_UNLIKELY(!ls_id.is_valid() || OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(ls_id), K(tenant_id));
  } else if (OB_FAIL(get_ls_recovery_stat(tenant_id, ls_id, true,
                                          old_ls_recovery, trans))) {
    LOG_WARN("failed to get ls current recovery stat", KR(ret), K(tenant_id),
             K(ls_id), K(old_ls_recovery));
  } else if (OB_UNLIKELY(SCN::base_scn() != old_ls_recovery.get_drop_scn()
                         && (old_ls_recovery.get_readable_scn() < old_ls_recovery.get_drop_scn()
                             || old_ls_recovery.get_sync_scn() < old_ls_recovery.get_drop_scn()))) {
    ret = OB_NEED_RETRY;
    LOG_WARN("can not drop ls while sync or readable ts not larger to drop ts", KR(ret), K(old_ls_recovery));
  } else {
    common::ObSqlString sql;
    if (OB_FAIL(sql.assign_fmt("DELETE from %s where ls_id = %ld and tenant_id = %lu",
                               OB_ALL_LS_RECOVERY_STAT_TNAME, ls_id.id(), tenant_id))) {
      LOG_WARN("failed to assign sql", KR(ret), K(ls_id), K(sql));
    } else if (OB_FAIL(exec_write(tenant_id, sql, this, trans))) {
      LOG_WARN("failed to exec write", KR(ret), K(tenant_id), K(ls_id), K(sql));
    }
    LOG_INFO("[LS_RECOVERY] drop ls", KR(ret), K(tenant_id), K(ls_id));
  }
  return ret;
}
int ObLSRecoveryStatOperator::update_ls_recovery_stat(
    const ObLSRecoveryStat &ls_recovery,
    ObMySQLProxy &proxy)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_recovery.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(ls_recovery));
  } else {
    ObMySQLTransaction trans;
    const uint64_t exec_tenant_id = get_exec_tenant_id(ls_recovery.get_tenant_id());
    if (OB_FAIL(trans.start(&proxy, exec_tenant_id))) {
      LOG_WARN("failed to start trans", KR(ret), K(exec_tenant_id), K(ls_recovery));
    } else if (OB_FAIL(update_ls_recovery_stat_in_trans(ls_recovery, trans))) {
      LOG_WARN("update ls recovery in trans", KR(ret), K(ls_recovery));
    }
    if (trans.is_started()) {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_WARN("failed to commit trans", KR(ret), KR(tmp_ret));
        ret = OB_SUCC(ret) ? tmp_ret : ret;
      }
    }
  }
  return ret;
}

int ObLSRecoveryStatOperator::update_ls_recovery_stat_in_trans(
    const ObLSRecoveryStat &ls_recovery,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObLSRecoveryStat old_ls_recovery;
  ObAllTenantInfo tenant_info;
  if (OB_UNLIKELY(!ls_recovery.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(ls_recovery));
  } else if (OB_FAIL(get_ls_recovery_stat(ls_recovery.get_tenant_id(), ls_recovery.get_ls_id(),
        true, old_ls_recovery, trans))) {
    LOG_WARN("failed to get ls current recovery stat", KR(ret), K(ls_recovery));
  } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(ls_recovery.get_tenant_id(), &trans,
                     true /* for update */, tenant_info))) {
    LOG_WARN("failed to load tenant info", KR(ret), K(ls_recovery));
  } else if (OB_UNLIKELY(!tenant_info.is_normal_status())) {
    ret = OB_NEED_RETRY;
    LOG_WARN("switchover status is not normal, do not update ls recovery", KR(ret),
             K(ls_recovery), K(tenant_info));
  } else {
    const SCN sync_scn = ls_recovery.get_sync_scn() > old_ls_recovery.get_sync_scn() ?
        ls_recovery.get_sync_scn() : old_ls_recovery.get_sync_scn();
    const SCN &readable_scn = ls_recovery.get_readable_scn() > old_ls_recovery.get_readable_scn() ?
        ls_recovery.get_readable_scn() : old_ls_recovery.get_readable_scn();
    common::ObSqlString sql;
    if (ls_recovery.get_ls_id() == share::SYS_LS && tenant_info.get_recovery_until_scn() < sync_scn) {
      ret = OB_NEED_RETRY;
      LOG_WARN("can not recovery bigger than recovery_until_scn", KR(ret), K(sync_scn), K(tenant_info));
    } else if (OB_FAIL(sql.assign_fmt("UPDATE %s SET sync_scn = %lu, readable_scn = "
                               "%lu where ls_id = %ld and tenant_id = %lu",
                               OB_ALL_LS_RECOVERY_STAT_TNAME, sync_scn.get_val_for_inner_table_field(),
                               readable_scn.get_val_for_inner_table_field(),
                               ls_recovery.get_ls_id().id(), ls_recovery.get_tenant_id()))) {
      LOG_WARN("failed to assign sql", KR(ret), K(sync_scn), K(readable_scn), K(sql));
    } else if (OB_FAIL(exec_write(ls_recovery.get_tenant_id(), sql, this, trans, true))) {
      LOG_WARN("failed to exec write", KR(ret), K(ls_recovery), K(sql));
    }
  }
  return ret;
}
int ObLSRecoveryStatOperator::set_ls_offline(const uint64_t &tenant_id,
                                             const share::ObLSID &ls_id,
                                             const ObLSStatus &ls_status,
                                             const SCN &drop_scn,
                                             const ObTenantSwitchoverStatus &working_sw_status,
                                             ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  UNUSEDx(ls_status, working_sw_status);
  if (OB_UNLIKELY(!ls_id.is_valid() || OB_INVALID_TENANT_ID == tenant_id
                  || !drop_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(ls_id), K(tenant_id), K(drop_scn));
  } else {
    common::ObSqlString sql;
    //drop ts can not set twice
    if (OB_FAIL(sql.assign_fmt("UPDATE %s SET drop_scn = %lu where ls_id = "
                               "%ld and tenant_id = %lu and drop_scn = %lu",
                               OB_ALL_LS_RECOVERY_STAT_TNAME, drop_scn.get_val_for_inner_table_field(),
                               ls_id.id(), tenant_id, OB_BASE_SCN_TS_NS))) {
      LOG_WARN("failed to assign sql", KR(ret), K(ls_id), K(tenant_id), K(sql));
    } else if (OB_FAIL(exec_write(tenant_id, sql, this, trans))) {
      LOG_WARN("failed to exec write", KR(ret), K(tenant_id), K(sql));
    }
  }

  LOG_INFO("[LS_RECOVERY]set ls drop ts", KR(ret), K(tenant_id), K(ls_id), K(drop_scn));

  return ret;
}

int ObLSRecoveryStatOperator::get_ls_recovery_stat(
      const uint64_t &tenant_id,
      const share::ObLSID &ls_id,
      const bool need_for_update,
      ObLSRecoveryStat &ls_recvery, 
      ObISQLClient &client)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid() || OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(ls_id), K(tenant_id));
  } else {
    common::ObSqlString sql;
    ObSEArray<ObLSRecoveryStat, 1> ls_recovery_array;
    if (OB_FAIL(sql.assign_fmt("select * from %s where ls_id = %ld and tenant_id = %lu",
               OB_ALL_LS_RECOVERY_STAT_TNAME, ls_id.id(), tenant_id))) {
      LOG_WARN("failed to assign sql", KR(ret), K(sql));
    } else if (need_for_update && OB_FAIL(sql.append(" for update"))) {
      LOG_WARN("failed to append sql", KR(ret), K(sql), K(need_for_update));
    } else if (OB_FAIL(exec_read(tenant_id, sql, client, this, ls_recovery_array))) {
      LOG_WARN("failed to read ls recovery", KR(ret), K(tenant_id), K(sql));
    } else if (0 == ls_recovery_array.count()) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("ls not exist", KR(ret), K(tenant_id), K(ls_id));
    } else if (OB_UNLIKELY(1 != ls_recovery_array.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("more than one ls is unexpected", KR(ret), K(ls_recovery_array), K(sql));
    } else if (OB_FAIL(ls_recvery.assign(ls_recovery_array.at(0)))) {
      LOG_WARN("failed to assign ls attr", KR(ret), K(ls_recovery_array));
    }
  }

  return ret;
}


int ObLSRecoveryStatOperator::fill_cell(common::sqlclient::ObMySQLResult*result, ObLSRecoveryStat &ls_recovery)
{
  int ret = OB_SUCCESS;
  ls_recovery.reset();
  if (OB_ISNULL(result)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("result is null", KR(ret));
  } else {
    int64_t id_value = OB_INVALID_ID;
    uint64_t tenant_id = OB_INVALID_TENANT_ID;
    int64_t sync_ts = OB_INVALID_SCN_VAL;
    int64_t readable_ts = OB_INVALID_SCN_VAL;
    int64_t create_ts = OB_INVALID_SCN_VAL;
    int64_t drop_ts = OB_INVALID_SCN_VAL;
    EXTRACT_INT_FIELD_MYSQL(*result, "tenant_id", tenant_id, uint64_t);
    EXTRACT_INT_FIELD_MYSQL(*result, "ls_id", id_value, int64_t);
    EXTRACT_UINT_FIELD_MYSQL(*result, "sync_scn", sync_ts, int64_t);
    EXTRACT_UINT_FIELD_MYSQL(*result, "readable_scn", readable_ts, int64_t);
    EXTRACT_UINT_FIELD_MYSQL(*result, "create_scn", create_ts, int64_t);
    EXTRACT_UINT_FIELD_MYSQL(*result, "drop_scn", drop_ts, int64_t);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to get result", KR(ret), K(id_value), K(tenant_id),
               K(sync_ts), K(readable_ts), K(create_ts), K(drop_ts));
    } else {
      ObLSID ls_id(id_value);
      SCN sync_scn;
      SCN readable_scn;
      SCN create_scn;
      SCN drop_scn;

      if (OB_FAIL(sync_scn.convert_for_inner_table_field(sync_ts))) {
        LOG_WARN("failed to convert_for_inner_table_field", KR(ret), K(tenant_id), K(id_value), K(sync_ts));
      } else if (OB_FAIL(readable_scn.convert_for_inner_table_field(readable_ts))) {
        LOG_WARN("failed to convert_for_inner_table_field", KR(ret), K(tenant_id), K(id_value), K(readable_ts));
      } else if (OB_FAIL(create_scn.convert_for_inner_table_field(create_ts))) {
        LOG_WARN("failed to convert_for_inner_table_field", KR(ret), K(tenant_id), K(id_value), K(create_ts));
      } else if (OB_FAIL(drop_scn.convert_for_inner_table_field(drop_ts))) {
        LOG_WARN("failed to convert_for_inner_table_field", KR(ret), K(tenant_id), K(id_value), K(drop_ts));
      } else if (OB_FAIL(ls_recovery.init(tenant_id, ls_id, sync_scn, readable_scn, create_scn, drop_scn))) {
        LOG_WARN("failed to init ls operation", KR(ret), K(tenant_id), K(sync_scn), K(readable_scn),
                 K(ls_id), K(create_scn), K(drop_scn));
      }
    }
  }
  return ret;
}

int ObLSRecoveryStatOperator::get_tenant_recovery_stat(const uint64_t tenant_id,
                                                       ObISQLClient &client,
                                                       SCN &sync_scn,
                                                       SCN &min_wrs)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(tenant_id));
  } else {
    common::ObSqlString sql;
    const uint64_t exec_tenant_id = get_exec_tenant_id(tenant_id);
    if (OB_FAIL(sql.assign_fmt(
            "select min(greatest(create_scn, sync_scn)) as sync_scn, "
            "min(greatest(create_scn, readable_scn)) as min_wrs from %s where "
            "(drop_scn = %ld or drop_scn > sync_scn) and tenant_id = %lu ",
            OB_ALL_LS_RECOVERY_STAT_TNAME, SCN::base_scn().get_val_for_inner_table_field(), tenant_id))) {
      LOG_WARN("failed to assign sql", KR(ret), K(sql), K(tenant_id));
    } else if (OB_FAIL(get_all_ls_recovery_stat_(tenant_id, sql, client, sync_scn, min_wrs))) {
      LOG_WARN("failed to get tenant stat", KR(ret), K(tenant_id), K(sql));
    }
  }
  return ret;
}

int ObLSRecoveryStatOperator::get_tenant_max_sync_scn(const uint64_t tenant_id,
                                                       ObISQLClient &client,
                                                       SCN &max_sync_scn)
{
  int ret = OB_SUCCESS;
  max_sync_scn.set_invalid();
  SCN dummy_min_wrs;
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(tenant_id));
  } else {
    common::ObSqlString sql;
    const uint64_t exec_tenant_id = get_exec_tenant_id(tenant_id);
    if (OB_FAIL(sql.assign_fmt(
            "select max(greatest(create_scn, sync_scn, drop_scn)) as sync_scn, "
            "min(greatest(create_scn, readable_scn)) as min_wrs from %s where tenant_id = %lu ",
            OB_ALL_LS_RECOVERY_STAT_TNAME, tenant_id))) {
      LOG_WARN("failed to assign sql", KR(ret), K(sql), K(tenant_id));
    } else if (OB_FAIL(get_all_ls_recovery_stat_(tenant_id, sql, client, max_sync_scn, dummy_min_wrs))) {
      LOG_WARN("failed to get tenant stat", KR(ret), K(tenant_id), K(sql));
    }
  }
  return ret;
}

int ObLSRecoveryStatOperator::get_user_ls_sync_scn(const uint64_t tenant_id,
                                                   ObISQLClient &client,
                                                   SCN &sync_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(tenant_id));
  } else {
    common::ObSqlString sql;
    SCN min_wrs_scn;
    const uint64_t exec_tenant_id = get_exec_tenant_id(tenant_id);
    if (OB_FAIL(sql.assign_fmt(
            "select min(greatest(create_scn, sync_scn)) as sync_scn, "
            "min(greatest(create_scn, readable_scn)) as min_wrs from %s where "
            "(drop_scn = %ld or drop_scn > sync_scn) and tenant_id = %lu and ls_id != %ld",
            OB_ALL_LS_RECOVERY_STAT_TNAME, SCN::base_scn().get_val_for_inner_table_field(), tenant_id, SYS_LS.id()))) {
      LOG_WARN("failed to assign sql", KR(ret), K(sql), K(tenant_id));
    } else if (OB_FAIL(get_all_ls_recovery_stat_(tenant_id, sql, client, sync_scn, min_wrs_scn))) {
      LOG_WARN("failed to get tenant stat", KR(ret), K(tenant_id), K(sql));
    }
  }
  return ret;
}

int ObLSRecoveryStatOperator::get_all_ls_recovery_stat_(
                                const uint64_t tenant_id,
                                const common::ObSqlString &sql,
                                ObISQLClient &client,
                                SCN &sync_scn,
                                SCN &min_wrs_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid_argument", KR(ret), K(tenant_id));
  } else {
    const uint64_t exec_tenant_id = get_exec_tenant_id(tenant_id);
    HEAP_VAR(ObMySQLProxy::MySQLResult, res) {
      common::sqlclient::ObMySQLResult *result = NULL;
      if (OB_FAIL(client.read(res, exec_tenant_id, sql.ptr()))) {
        LOG_WARN("failed to read", KR(ret), K(exec_tenant_id), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get sql result", KR(ret));
      } else if (OB_FAIL(result->next())) {
        LOG_WARN("failed to get tenant info", KR(ret), K(sql));
      } else {
        uint64_t sync_scn_val = OB_INVALID_SCN_VAL;
        uint64_t min_wrs_val = OB_INVALID_SCN_VAL;
        EXTRACT_UINT_FIELD_MYSQL(*result, "sync_scn", sync_scn_val, uint64_t);
        EXTRACT_UINT_FIELD_MYSQL(*result, "min_wrs", min_wrs_val, uint64_t);
        if (OB_FAIL(ret)) {
          LOG_WARN("failed to get tenant stat", KR(ret));
        } else if (OB_FAIL(sync_scn.convert_for_inner_table_field(sync_scn_val))) {
          LOG_WARN("failed to convert_for_inner_table_field", KR(ret), K(sync_scn_val), K(tenant_id));
        } else if (OB_FAIL(min_wrs_scn.convert_for_inner_table_field(min_wrs_val))) {
          LOG_WARN("failed to convert_for_inner_table_field", KR(ret), K(min_wrs_val), K(tenant_id));
        } else {/*do nothing*/}
      }
      if (OB_FAIL(ret)) {
        LOG_WARN("failed to get tenant stat", KR(ret));
      }
    }
  }
  return ret;
}

}//end of share
}//end of ob

